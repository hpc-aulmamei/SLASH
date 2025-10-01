/**
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "slash_ctldev.h"

#include <linux/atomic.h>
#include <linux/capability.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include "slash.h"
#include "slash_dmabuf.h"

static int slash_ctldev_set_bar_info(struct pci_dev *pdev, struct slash_ctldev *ctldev);
static int slash_ctldev_create_bar_dmabufs(struct slash_ctldev *ctldev);
static int slash_ctldev_create_misc(struct slash_ctldev *ctldev);

static void slash_ctldev_destroy_misc(struct slash_ctldev *ctldev);
static void slash_ctldev_destroy_dmabufs(struct slash_ctldev *ctldev);

static long slash_ctldev_fop_ioctl(struct file *, unsigned int, unsigned long);

static atomic_t slash_ctldev_devcount = ATOMIC_INIT(0);

static struct file_operations slash_ctldev_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = slash_ctldev_fop_ioctl,
};

int slash_ctldev_create(struct pci_dev *pdev)
{   
    int err;

    struct slash_ctldev *ctldev = kzalloc(sizeof(*ctldev), GFP_KERNEL);
    if (!ctldev) {
        dev_err(&pdev->dev, "ctldev: kzalloc failed\n");
        return -ENOMEM;
    }
    ctldev->pdev = pdev;

    dev_info(&pdev->dev, "ctldev: creating control device\n");

    pci_set_drvdata(pdev, ctldev);

    err = slash_ctldev_set_bar_info(pdev, ctldev);
    if (err) {
        dev_err(&pdev->dev, "ctldev: set_bar_info failed: %d\n", err);
        goto err_free_ctldev;
    }

    err = slash_ctldev_create_bar_dmabufs(ctldev);
    if (err) {
        dev_err(&pdev->dev, "ctldev: creating BAR dma-bufs failed: %d\n", err);
        /**
         * We go here because there may be some dmabufs to free
         * if some succeded and some failed.
         */
        goto err_destroy_dmabufs;
    }

    err = slash_ctldev_create_misc(ctldev);
    if (err) {
        dev_err(&pdev->dev, "ctldev: creating misc ctldev failed: %d\n", err);
        goto err_destroy_dmabufs;
    }

    dev_info(&pdev->dev, "ctldev: device created successfully\n");

    return 0;

err_destroy_dmabufs:
    slash_ctldev_destroy_dmabufs(ctldev);

err_free_ctldev:
    kfree(ctldev);

    return err;
}

static int slash_ctldev_set_bar_info(struct pci_dev *pdev, struct slash_ctldev *ctldev)
{
    int i;

    dev_dbg(&pdev->dev, "ctldev: probing PCI BARs\n");
    for (i = 0; i < PCI_STD_NUM_BARS; i++) {
        unsigned long flags;

        if (!pci_resource_start(pdev, i)) {
            dev_dbg(&pdev->dev, "ctldev: BAR%d unused\n", i);
            continue; /* Unused BAR */
        }

        ctldev->bars[i].active = 1;
        ctldev->bars[i].start  = pci_resource_start(pdev, i);
        ctldev->bars[i].end    = pci_resource_end(pdev, i);
        ctldev->bars[i].len    = pci_resource_len(pdev, i);
        flags                  = pci_resource_flags(pdev, i);
        ctldev->bars[i].mmio   = ((flags & IORESOURCE_MEM) != 0);


        dev_info(&pdev->dev,
                "Found BAR%d: 0x%pa - 0x%pa (size: %pa) %s\n",
                i, &ctldev->bars[i].start, &ctldev->bars[i].end, &ctldev->bars[i].len,
                (flags & IORESOURCE_MEM) ? "MMIO" :
                (flags & IORESOURCE_IO) ? "IO" : "UNKNOWN");
    }

    return 0;
}

static int slash_ctldev_create_bar_dmabufs(struct slash_ctldev *ctldev)
{
    int i;
    struct dma_buf *dmabuf;

    dev_dbg(&ctldev->pdev->dev, "ctldev: creating dma-bufs for MMIO BARs\n");
    for (i = 0; i < PCI_STD_NUM_BARS; i++) {
        if (!ctldev->bars[i].active || !ctldev->bars[i].mmio) {
            continue;
        }

        dmabuf = slash_bar_dmabuf_create(ctldev->pdev, i);
        if (IS_ERR(dmabuf)) {
            dev_err(&ctldev->pdev->dev, "ctldev: BAR%d dmabuf create failed: %ld\n", i, PTR_ERR(dmabuf));
            return PTR_ERR(dmabuf);
        }

        ctldev->bars[i].dmabuf = dmabuf;
        dev_dbg(&ctldev->pdev->dev, "ctldev: BAR%d dmabuf created\n", i);
    }

    return 0;
}

static int slash_ctldev_create_misc(struct slash_ctldev *ctldev)
{
    int err, id;
    const char *name, *nodename;
    
    name = kasprintf(GFP_KERNEL, SLASH_CTLDEV_NAME_FMT, pci_name(ctldev->pdev));
    if (!name) {
        dev_err(&ctldev->pdev->dev, "ctldev: kasprintf(name) failed\n");
        return -ENOMEM;
    }

    id = atomic_inc_return(&slash_ctldev_devcount) - 1;
    nodename = kasprintf(GFP_KERNEL, SLASH_CTLDEV_NODENAME_FMT, id);
    if (!nodename) {
        dev_err(&ctldev->pdev->dev, "ctldev: kasprintf(nodename) failed\n");

        err = -ENOMEM;
        goto err_free_name;
    }

    ctldev->misc.minor = MISC_DYNAMIC_MINOR;
    ctldev->misc.name = name;
    ctldev->misc.fops = &slash_ctldev_fops;
    ctldev->misc.parent = &ctldev->pdev->dev;
    ctldev->misc.nodename = nodename;
    ctldev->misc.mode = SLASH_CTLDEV_MODE;

    err = misc_register(&ctldev->misc);
    if (err) {
        dev_err(&ctldev->pdev->dev, "ctldev: misc_register failed: %d\n", err);
        goto err_free_nodename;
    }

    return 0;

err_free_nodename:
    kfree(nodename);

err_free_name:
    kfree(name);

    return err;
}

void slash_ctldev_destroy(struct pci_dev *pdev)
{
    struct slash_ctldev *ctldev = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "ctldev: destroying control device\n");
    slash_ctldev_destroy_misc(ctldev);
    slash_ctldev_destroy_dmabufs(ctldev);

    kfree(ctldev);
}

static void slash_ctldev_destroy_misc(struct slash_ctldev *ctldev)
{
    dev_dbg(&ctldev->pdev->dev, "ctldev: deregistering misc device\n");
    misc_deregister(&ctldev->misc);
    kfree(ctldev->misc.name);
    kfree(ctldev->misc.nodename);
    ctldev->misc.name = NULL;
    ctldev->misc.nodename = NULL;
}

static void slash_ctldev_destroy_dmabufs(struct slash_ctldev *ctldev)
{
    int i;

    for (i = 0; i < PCI_STD_NUM_BARS; i++) {
        if (ctldev->bars[i].dmabuf) {
            dev_dbg(&ctldev->pdev->dev, "ctldev: destroying BAR%d dmabuf\n", i);
            slash_bar_dmabuf_destroy(ctldev->bars[i].dmabuf);
        }
    }
}

static long slash_ctldev_fop_ioctl(struct file *file, unsigned int op, unsigned long arg)
{
    struct miscdevice *misc = file->private_data;
    struct pci_dev *pdev = to_pci_dev(misc->parent);
    struct slash_ctldev *ctldev = pci_get_drvdata(pdev);

    dev_dbg(&pdev->dev, "ctldev: ioctl op=0x%x\n", op);
    switch(op) {
    case SLASH_CTLDEV_IOCTL_GET_BAR_INFO: {
        struct slash_ioctl_bar_info bar_info = {0};
        struct slash_ctldev_bar *bar = NULL;
        u32 bar_info_alleged_size;

        if (copy_from_user(&bar_info_alleged_size), (void __user *) arg, sizeof(bar_info_alleged_size)) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_INFO copy_from_user failed\n");
            return -EFAULT;
        }

        if (copy_struct_from_user(&bar_info, sizeof(bar_info), (void __user *) arg, bar_info_alleged_size)) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_INFO copy_from_user failed\n");
            return -EFAULT;
        }

        if (bar_info.bar_number < 0 || bar_info.bar_number >= PCI_STD_NUM_BARS) {
            dev_warn(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_INFO invalid BAR %d\n", bar_info.bar_number);
            return -EINVAL;
        }

        bar = &ctldev->bars[bar_info.bar_number];

        bar_info.usable = bar->active && bar->mmio;
        bar_info.in_use = 0;
        bar_info.start_address = bar->start;
        bar_info.length = bar->len;

        if (copy_struct_to_user((void __user *) arg, bar_info_alleged_size, &bar_info, sizeof(bar_info), NULL)) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_INFO copy_to_user failed\n");
            return -EFAULT;
        }

        return 0;
    }

    case SLASH_CTLDEV_IOCTL_GET_BAR_FD: {
        struct slash_ioctl_bar_fd_request fd_request = {0};
        struct slash_ctldev_bar *bar = NULL;
        int ret;
        u32 

        if (!capable(CAP_SYS_RAWIO)) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD capability check failed\n");
            return -EPERM;
        }

        if (copy_from_user(&fd_request, (void __user *) arg, sizeof(fd_request))) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD copy_from_user failed\n");
            return -EFAULT;
        }

        if (fd_request.bar_number < 0 || fd_request.bar_number >= PCI_STD_NUM_BARS) {
            dev_warn(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD invalid BAR %d\n", fd_request.bar_number);
            return -EINVAL;
        }
        if (fd_request.flags & ~O_CLOEXEC) {
            dev_warn(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD invalid flags 0x%x\n", fd_request.flags);
            return -EINVAL;
        }

        bar = &ctldev->bars[fd_request.bar_number];

        if (!bar->dmabuf) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD BAR%d has no dmabuf\n", fd_request.bar_number);
            return -ENODEV;
        }

        fd_request.length = bar->len;

        if (copy_to_user((void __user *) arg, &fd_request, sizeof(fd_request))) {
            dev_err(&pdev->dev, "ctldev: SLASH_CTLDEV_IOCTL_GET_BAR_FD copy_to_user failed\n");
            return -EFAULT;
        }

        get_dma_buf(bar->dmabuf);
        ret = dma_buf_fd(bar->dmabuf, fd_request.flags);
        if (ret < 0) {
            dev_err(&pdev->dev, "ctldev: GET_BAR_FD dma_buf_fd failed: %d\n", ret);
            dma_buf_put(bar->dmabuf);
            return ret;
        }

        dev_dbg(&pdev->dev, "ctldev: GET_BAR_FD BAR%d -> fd %d\n", fd_request.bar_number, ret);
        return ret;
    }

    default:
        dev_warn(&pdev->dev, "ctldev: unknown ioctl op=0x%x\n", op);
        return -ENOTTY;
    }
}
