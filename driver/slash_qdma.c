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

#include "slash_qdma.h"

#include "libqdma_export.h"

#include "slash.h"

#include <asm/cacheflush.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>
#include <linux/anon_inodes.h>

#define SLASH_QDMA_PF 0

#define SLASH_QDMA_DIR_H2C  BIT(0)
#define SLASH_QDMA_DIR_C2H  BIT(1)
#define SLASH_QDMA_DIR_CMPT BIT(2)
#define SLASH_QDMA_DIR_MASK (SLASH_QDMA_DIR_H2C | SLASH_QDMA_DIR_C2H | \
                             SLASH_QDMA_DIR_CMPT)

#define SLASH_QDMA_QTYPE_COUNT (Q_CMPT + 1)

#define SLASH_QDMA_MAX_QPAIRS 256
#define SLASH_QDMA_QPAIR_ID_RANGE XA_LIMIT(0, SLASH_QDMA_MAX_QPAIRS - 1)

struct slash_qdma_dev;

struct slash_qdma_qpair_entry {
    struct kref ref;
    unsigned long qhndl[SLASH_QDMA_QTYPE_COUNT];
    u32 dir_mask;
    enum qdma_q_mode mode;
    u32 irq_mode;
    u32 irq_vector;
};

struct slash_qdma_dev {
    struct pci_dev *pdev;
    unsigned long qdma_handle;

    struct miscdevice misc;
    struct kref ref;
    struct mutex lock;
    struct xarray qpairs;

    /*
     * Initialization booleans.
     * Assume these are always true outside of create/destroy.
     */
    bool have_qdma_handle;
    bool is_misc_registered;
    bool hw_shutdown;
};

typedef int (*slash_qdma_queue_cmd_fn)(unsigned long qdma_handle,
                                       unsigned long qhndl,
                                       char *errbuf,
                                       int errbuf_sz);

__attribute__((unused))
static enum queue_type_t slash_qdma_dir_to_qtype(u32 dir_bit)
{
    switch (dir_bit) {
    case SLASH_QDMA_DIR_H2C:
        return Q_H2C;
    case SLASH_QDMA_DIR_C2H:
        return Q_C2H;
    case SLASH_QDMA_DIR_CMPT:
        return Q_CMPT;
    default:
        return Q_H2C; /* should never reach */
    }
}

static u32 slash_qdma_qtype_to_dir(enum queue_type_t qtype)
{
    switch (qtype) {
    case Q_H2C:
        return SLASH_QDMA_DIR_H2C;
    case Q_C2H:
        return SLASH_QDMA_DIR_C2H;
    case Q_CMPT:
        return SLASH_QDMA_DIR_CMPT;
    default:
        return 0;
    }
}

static inline struct slash_qdma_qpair_entry *
slash_qdma_qpair_lookup(struct slash_qdma_dev *qdma_dev, u32 qid)
{
    return xa_load(&qdma_dev->qpairs, qid);
}

static void slash_qdma_qpair_entry_release(struct kref *ref)
{
    struct slash_qdma_qpair_entry *entry =
        container_of(ref, struct slash_qdma_qpair_entry, ref);

    kfree(entry);
}

static inline void slash_qdma_qpair_get(struct slash_qdma_qpair_entry *entry)
{
    kref_get(&entry->ref);
}

static inline void slash_qdma_qpair_put(struct slash_qdma_qpair_entry *entry)
{
    kref_put(&entry->ref, slash_qdma_qpair_entry_release);
}

static inline int
slash_qdma_qpair_insert(struct slash_qdma_dev *qdma_dev, struct slash_qdma_qpair_entry *entry, u32 *id)
{
    kref_init(&entry->ref);
    return xa_alloc(&qdma_dev->qpairs, id, entry, SLASH_QDMA_QPAIR_ID_RANGE, GFP_KERNEL);
}

static inline void
slash_qdma_qpair_remove(struct slash_qdma_dev *qdma_dev, u32 qid)
{
    struct slash_qdma_qpair_entry *entry;

    entry = xa_erase(&qdma_dev->qpairs, qid);
    if (entry)
        slash_qdma_qpair_put(entry);
}

struct slash_qdma_qpair_file_ctx {
    struct slash_qdma_dev *qdma_dev;
    struct slash_qdma_qpair_entry *entry;
    u32 qid;
};

struct slash_qdma_io_cb {
    void __user *buf;
    size_t len;
    unsigned int pages_nr;
    struct qdma_sw_sg *sgl;
    struct page **pages;
    struct qdma_request req;
};

static int slash_qdma_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void slash_qdma_remove(struct pci_dev *pdev);
static int slash_qdma_create_qdma_device(struct pci_dev *pdev, struct slash_qdma_dev **pdevice);
static void slash_qdma_destroy_qdma_device(struct slash_qdma_dev *device);
static void slash_qdma_dev_release(struct kref *ref);
static void slash_qdma_conf_options(struct qdma_dev_conf *conf, struct pci_dev *pdev);
static int slash_qdma_ioctl_info_w(struct miscdevice *misc,
                                   struct slash_qdma_dev *qdma_dev,
                                   void __user *uarg);
static int slash_qdma_ioctl_qpair_add_w(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         void __user *uarg);
static int slash_qdma_ioctl_qpair_add(struct miscdevice *misc,
                                      struct slash_qdma_dev *qdma_dev,
                                      struct slash_qdma_qpair_add *req);
static int slash_qdma_ioctl_qpair_add_q(struct miscdevice *misc,
                                        struct slash_qdma_dev *qdma_dev,
                                        struct slash_qdma_qpair_add *req,
                                        struct slash_qdma_qpair_entry *entry,
                                        enum queue_type_t qtype);
static void slash_qdma_ioctl_qpair_rm_q(struct miscdevice *misc,
                                        struct slash_qdma_dev *qdma_dev,
                                        struct slash_qdma_qpair_entry *entry,
                                        enum queue_type_t qtype);
static int slash_qdma_ioctl_qpair_op_w(struct miscdevice *misc,
                                       struct slash_qdma_dev *qdma_dev,
                                       void __user *uarg);
static int slash_qdma_ioctl_qpair_op(struct miscdevice *misc,
                                     struct slash_qdma_dev *qdma_dev,
                                     struct slash_qdma_qpair_op *req);
static int slash_qdma_ioctl_qpair_op_apply(struct slash_qdma_dev *qdma_dev,
                                           struct slash_qdma_qpair_entry *entry,
                                           struct slash_qdma_qpair_op *req,
                                           slash_qdma_queue_cmd_fn fn,
                                           const char *op_name,
                                           bool stop_on_err);
static int slash_qdma_ioctl_qpair_get_fd_w(struct miscdevice *misc,
                                           struct slash_qdma_dev *qdma_dev,
                                           void __user *uarg);

static ssize_t slash_qdma_qpair_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos);
static ssize_t slash_qdma_qpair_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos);
static int slash_qdma_qpair_release(struct inode *inode, struct file *file);
static long slash_qdma_qpair_ioctl(struct file *file,
                                   unsigned int cmd, unsigned long arg);

static const struct file_operations slash_qdma_qpair_fops = {
    .owner          = THIS_MODULE,
    .read           = slash_qdma_qpair_read,
    .write          = slash_qdma_qpair_write,
    .unlocked_ioctl = slash_qdma_qpair_ioctl,
    .release        = slash_qdma_qpair_release,
    .llseek         = default_llseek,
};


static int slash_qdma_fop_open(struct inode *inode, struct file *file);
static int slash_qdma_fop_release(struct inode *inode, struct file *file);
static long slash_qdma_fop_ioctl(struct file *file, unsigned int op, unsigned long arg);
static void slash_qdma_ioctl_info(struct miscdevice *misc, struct slash_qdma_dev *qdma_dev, struct slash_qdma_info *qdma_info);

#define SLASH_QDMA_PCI_VENDOR_ID 0x00 //TOOD: Change
#define SLASH_QDMA_PCI_DEVICE_ID 0x00

static const struct pci_device_id slash_qdma_ids[] = {
    {PCI_DEVICE(SLASH_QDMA_PCI_VENDOR_ID, SLASH_QDMA_PCI_DEVICE_ID)},
    {0,}
};
MODULE_DEVICE_TABLE(pci, slash_qdma_ids);

static struct pci_driver slash_qdma_driver = {
    .name = SLASH_NAME "_qdma",
    .id_table = slash_qdma_ids,
    .probe = slash_qdma_probe,
    .remove = slash_qdma_remove,
};

static struct file_operations slash_qdma_fops = {
    .owner          = THIS_MODULE,
    .open           = slash_qdma_fop_open,
    .release        = slash_qdma_fop_release,
    .unlocked_ioctl = slash_qdma_fop_ioctl,
};

int __init slash_qdma_init(unsigned int num_threads, char *debugfs)
{
    int err;

    pr_debug("slash: initializing qdma\n");

    err = libqdma_init(num_threads, debugfs);
    if (err) {
        pr_err("slash: libqdma_init failed: %d\n", err);
        return err;
    }

    err = pci_register_driver(&slash_qdma_driver);
    if (err) {
        pr_err("slash: register qdma driver failed: %d\n", err);
        goto err_exit_libqdma;
    }

    return 0;

err_exit_libqdma:
    libqdma_exit();

    return err;
}

void __exit slash_qdma_exit(void)
{
    pr_debug("slash: deinitializing qdma\n");

    pci_unregister_driver(&slash_qdma_driver);

    libqdma_exit();
}

/* --- PCI Operations --- */

static int slash_qdma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int err;
    struct qdma_dev_conf conf;
    struct slash_qdma_dev *device = NULL;

    memset(&conf, 0, sizeof(conf));

    dev_info(&pdev->dev, "slash: qdma: probe start for %s\n", pci_name(pdev));
    dev_dbg(&pdev->dev, "slash: qdma: vendor=0x%04x device=0x%04x fn=%u\n", pdev->vendor, pdev->device, PCI_FUNC(pdev->devfn));

    if (PCI_FUNC(pdev->devfn) != SLASH_QDMA_PF) {
        dev_err(&pdev->dev, "slash: expected PF %u, got %u\n", SLASH_QDMA_PF, PCI_FUNC(pdev->devfn));
        return -EINVAL;
    }

    err = slash_qdma_create_qdma_device(pdev, &device);
    if (err) {
        goto err_free;
    }

    slash_qdma_conf_options(&conf, pdev);
    err = qdma_device_open(SLASH_NAME, &conf, &device->qdma_handle);
    if (err) {
        dev_err(&pdev->dev, "slash: qdma: could not open qdma device %d", err);
        goto err_free;
    }
    device->have_qdma_handle = true;

    err = misc_register(&device->misc);
    if (err) {
        dev_err(&pdev->dev, "slash: qdma: could not register misc device: %d", err);
        goto err_free;
    }
    device->is_misc_registered = true;

    return 0;

err_free:
    if (device) {
        slash_qdma_destroy_qdma_device(device);
        kref_put(&device->ref, slash_qdma_dev_release);
    }

    return err;
}

static void slash_qdma_remove(struct pci_dev *pdev)
{
    struct slash_qdma_dev *device = pci_get_drvdata(pdev);

    if (!device)
        return;

    slash_qdma_destroy_qdma_device(device);
    kref_put(&device->ref, slash_qdma_dev_release);
}

static int slash_qdma_create_qdma_device(struct pci_dev *pdev, struct slash_qdma_dev **pdevice)
{
    int err;
    struct slash_qdma_dev *device;
    static atomic_t devcount = ATOMIC_INIT(0);
    int id;

    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        return -ENOMEM;
    }
    device->pdev = pdev;
    kref_init(&device->ref);
    mutex_init(&device->lock);
    xa_init(&device->qpairs);
    device->hw_shutdown = false;
    pci_set_drvdata(pdev, device);

    { /* Miscdevice */
        device->misc.minor = MISC_DYNAMIC_MINOR;
        device->misc.fops = &slash_qdma_fops;
        device->misc.parent = &pdev->dev;
        device->misc.mode = SLASH_CTLDEV_QDMA_MODE;
    
        device->misc.name = kasprintf(GFP_KERNEL, SLASH_QDMA_CTLDEV_NAME_FMT, pci_name(device->pdev));
        if (!device->misc.name) {
            dev_err(&device->pdev->dev, "qdma: kasprintf(name) failed\n");
            err = -ENOMEM;
            goto err_free;
        }

        id = atomic_inc_return(&devcount) - 1;
        device->misc.nodename = kasprintf(GFP_KERNEL, SLASH_QDMA_CTLDEV_NODENAME_FMT, id);
        if (!device->misc.nodename) {
            dev_err(&device->pdev->dev, "qdma: kasprintf(nodename) failed\n");

            err = -ENOMEM;
            goto err_free;
        }
    }

    *pdevice = device;
    return 0;

err_free:
    slash_qdma_destroy_qdma_device(device);
    *pdevice = NULL;

    return err;
}

static void slash_qdma_destroy_qdma_device(struct slash_qdma_dev *device)
{
    int err;

    if (!device) {
        return;
    }

    mutex_lock(&device->lock);
    if (device->hw_shutdown) {
        mutex_unlock(&device->lock);
        return;
    }
    device->hw_shutdown = true;
    mutex_unlock(&device->lock);

    pci_set_drvdata(device->pdev, NULL);

    if (device->is_misc_registered) {
        misc_deregister(&device->misc);
        device->is_misc_registered = false;
    }

    mutex_lock(&device->lock);

    {
        struct slash_qdma_qpair_entry *entry;
        unsigned long index;
        unsigned int idx;

        xa_for_each(&device->qpairs, index, entry) {
            for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
                enum queue_type_t qtype = idx;
                u32 dir_bit = slash_qdma_qtype_to_dir(qtype);

                if (!(entry->dir_mask & dir_bit))
                    continue;

                slash_qdma_ioctl_qpair_rm_q(&device->misc, device, entry, qtype);
            }
            xa_erase(&device->qpairs, index);
            slash_qdma_qpair_put(entry);
        }
        xa_destroy(&device->qpairs);
    }

    if (device->have_qdma_handle) {
        err = qdma_device_close(device->pdev, device->qdma_handle);
        if (err) {
            dev_err(&device->pdev->dev, "Error in qdma_device_close: %d\n", err);
        }
        device->have_qdma_handle = false;
    }

    mutex_unlock(&device->lock);
}

static void slash_qdma_dev_release(struct kref *ref)
{
    struct slash_qdma_dev *device =
        container_of(ref, struct slash_qdma_dev, ref);

    mutex_destroy(&device->lock);

    if (device->misc.name) {
        kfree(device->misc.name);
    }

    if (device->misc.nodename) {
        kfree(device->misc.nodename);
    }

    kfree(device);
}

static void slash_qdma_conf_options(struct qdma_dev_conf *conf, struct pci_dev *pdev)
{
    conf->pdev               = pdev;
    conf->qsets_max          = 256; /* Maximum number of queue paris. Might be lowered. TODO: tune */
    conf->zerolen_dma        = 0; /* Disallow 0-length transfers */
    conf->master_pf          = 1; /* This is the master PF */
    conf->intr_moderation    = 1;
    conf->vf_max             = 8;
    conf->intr_rngsz         = 128; // TODO: tune

    // Ask for as many queue MSI-X vectors as you’d like to dedicate to queues
    conf->msix_qvec_max      = 256;
    conf->user_msix_qvec_max = 0;
    conf->data_msix_qvec_max = 0;

    conf->qdma_drv_mode      = POLL_MODE; // TODO: experiment with this
    conf->uld                = 0;

    conf->bar_num_config     = 0;
    conf->bar_num_user       = -1;
    conf->bar_num_bypass     = -1;
    conf->qsets_base         = 0;

    // Optional callbacks
    conf->fp_user_isr_handler = NULL;
    conf->fp_q_isr_top_dev    = NULL;
    conf->fp_flr_free_resource= NULL;
    conf->debugfs_dev_root    = NULL;
}

/* --- Miscdevice operations --- */
static long slash_qdma_fop_ioctl(struct file *file, unsigned int op, unsigned long arg)
{
    struct slash_qdma_dev *qdma_dev = file->private_data;
    struct miscdevice *misc = &qdma_dev->misc;
    void __user *uarg = (void __user *)arg;
    long ret = 0;

    if (!qdma_dev)
        return -ENODEV;

    dev_dbg(&qdma_dev->pdev->dev, "qdma: ioctl op=0x%x\n", op);

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    mutex_unlock(&qdma_dev->lock);

    switch (op) {
    case SLASH_QDMA_IOCTL_INFO:
        ret = slash_qdma_ioctl_info_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_QPAIR_ADD:
        ret = slash_qdma_ioctl_qpair_add_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_Q_OP:
        ret = slash_qdma_ioctl_qpair_op_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_QPAIR_GET_FD:
        ret = slash_qdma_ioctl_qpair_get_fd_w(misc, qdma_dev, uarg);
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

static int slash_qdma_fop_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct slash_qdma_dev *qdma_dev =
        container_of(misc, struct slash_qdma_dev, misc);
    int ret = 0;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        ret = -ENODEV;
    } else {
        kref_get(&qdma_dev->ref);
        file->private_data = qdma_dev;
    }
    mutex_unlock(&qdma_dev->lock);

    return ret;
}

static int slash_qdma_fop_release(struct inode *inode, struct file *file)
{
    struct slash_qdma_dev *qdma_dev = file->private_data;

    if (qdma_dev)
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);

    return 0;
}

static int slash_qdma_ioctl_info_w(struct miscdevice *misc,
                                    struct slash_qdma_dev *qdma_dev,
                                    void __user *uarg)
{
    struct slash_qdma_info info;
    u32 user_size = 0;
    size_t copy_size;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (!user_size || user_size > sizeof(info))
        user_size = sizeof(info);


    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    slash_qdma_ioctl_info(misc, qdma_dev, &info);
    mutex_unlock(&qdma_dev->lock);

    copy_size = min_t(size_t, user_size, sizeof(info));
    if (copy_to_user(uarg, &info, copy_size))
        return -EFAULT;

    return 0;
}

static void slash_qdma_ioctl_info(struct miscdevice *misc,
                                  struct slash_qdma_dev *qdma_dev,
                                  struct slash_qdma_info *qdma_info)
{
    (void) misc;
    (void) qdma_dev;

    qdma_info->qsets_max = 0;
    qdma_info->msix_qvecs = 0;
    qdma_info->vf_max = 0;
    qdma_info->caps = 0;
}

static int slash_qdma_ioctl_qpair_add_w(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         void __user *uarg)
{
    struct slash_qdma_qpair_add req;
    __u32 user_size = 0;
    size_t copy_size;
    u32 dir_mask;
    int err;

    /*
     * First, fetch the size field from userspace so we can
     * safely handle callers built against older or newer
     * versions of the struct.
     */
    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (!user_size || user_size > sizeof(req))
        user_size = sizeof(req);

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, user_size))
        return -EFAULT;

    dir_mask = req.dir_mask & SLASH_QDMA_DIR_MASK;
    if (!dir_mask || dir_mask != req.dir_mask)
        return -EINVAL;

    if (req.mode != QDMA_Q_MODE_MM && req.mode != QDMA_Q_MODE_ST)
        return -EINVAL;

    if (req.h2c_ring_sz >= 16 || req.c2h_ring_sz >= 16 || req.cmpt_ring_sz >= 16)
        return -EINVAL;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    err = slash_qdma_ioctl_qpair_add(misc, qdma_dev, &req);
    mutex_unlock(&qdma_dev->lock);

    if (err)
        return err;

    /*
     * On success, update the size field to reflect the
     * kernel's view of the struct and copy back only as
     * many bytes as the caller originally provided.
     */
    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size))
        return -EFAULT;

    return err;
}

static int slash_qdma_ioctl_qpair_add(struct miscdevice *misc,
                                      struct slash_qdma_dev *qdma_dev,
                                      struct slash_qdma_qpair_add *req)
{
    struct slash_qdma_qpair_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    unsigned int idx;
    bool added[SLASH_QDMA_QTYPE_COUNT] = {0};
    int ret = 0;

    if (!entry)
        return -ENOMEM;

    entry->mode = req->mode;
    entry->irq_mode = 0;
    entry->irq_vector = 0;

    /*
     * Allocate a new qpair ID in the xarray and use it as the
     * QDMA queue index for all queues in this pair. Any qid
     * value provided by userspace is ignored.
     */
    ret = slash_qdma_qpair_insert(qdma_dev, entry, &req->qid);
    if (ret) {
        dev_err(&qdma_dev->pdev->dev,
                "qdma: qpair insert failed: %d\n", ret);
        kfree(entry);
        return ret;
    }

    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        enum queue_type_t qtype = idx;
        u32 dir_bit = slash_qdma_qtype_to_dir(qtype);

        if (!(req->dir_mask & dir_bit))
            continue;

        ret = slash_qdma_ioctl_qpair_add_q(misc, qdma_dev, req, entry, qtype);
        if (ret)
            goto rollback;

        added[idx] = true;
    }

    return 0;

rollback:
    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        if (added[idx])
            slash_qdma_ioctl_qpair_rm_q(misc, qdma_dev, entry, idx);
    }

    slash_qdma_qpair_remove(qdma_dev, req->qid);

    return ret;
}

static int slash_qdma_ioctl_qpair_add_q(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         struct slash_qdma_qpair_add *req,
                                         struct slash_qdma_qpair_entry *entry,
                                         enum queue_type_t qtype)
{
    u32 dir_bit = slash_qdma_qtype_to_dir(qtype);
    struct qdma_queue_conf qconf = {0};
    char errbuf[128] = {0};
    u32 dir_mask = req->dir_mask;
    int err;
    unsigned long qhndl = 0;

    if (!(dir_mask & dir_bit))
        return -EINVAL;

    qconf.qidx = req->qid;
    qconf.q_type = qtype;
    qconf.st = (req->mode == QDMA_Q_MODE_ST);
    qconf.irq_en = 0;
    qconf.cmpl_en_intr = 0;
    qconf.cmpl_trig_mode = TRIG_MODE_DISABLE;

    switch (qtype) {
    case Q_H2C:
        qconf.desc_rng_sz_idx = req->h2c_ring_sz;
        break;
    case Q_C2H:
        qconf.desc_rng_sz_idx = req->c2h_ring_sz;
        qconf.cmpl_rng_sz_idx = req->cmpt_ring_sz;
        qconf.cmpl_desc_sz = CMPT_DESC_SZ_16B;
        break;
    case Q_CMPT:
        qconf.st = 0;
        qconf.desc_rng_sz_idx = req->cmpt_ring_sz;
        qconf.cmpl_rng_sz_idx = req->cmpt_ring_sz;
        qconf.cmpl_desc_sz = CMPT_DESC_SZ_16B;
        qconf.cmpl_en_intr = 0;
        break;
    default:
        break;
    }

    err = qdma_queue_add(qdma_dev->qdma_handle, &qconf, &qhndl,
                            errbuf, sizeof(errbuf));
    if (err) {
        dev_err(&qdma_dev->pdev->dev,
                "qdma: queue add failed (qid=%u, type=%u): %d (%s)\n",
                req->qid, qtype, err, errbuf);
        return err;
    }

    entry->qhndl[qtype] = qhndl;
    entry->dir_mask |= dir_bit;

    return 0;
}

static void slash_qdma_ioctl_qpair_rm_q(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         struct slash_qdma_qpair_entry *entry,
                                         enum queue_type_t qtype)
{
    unsigned long qhndl = entry->qhndl[qtype];
    char errbuf[128] = {0};
    int err;
    
    err = qdma_queue_remove(qdma_dev->qdma_handle, qhndl,
                        errbuf, sizeof(errbuf));

    if (err) {
        dev_err(&qdma_dev->pdev->dev,
                "qdma: queue remove failed (type=%u): %d (%s)\n",
                qtype, err, errbuf);
    }

    entry->qhndl[qtype] = 0;
    entry->dir_mask &= ~slash_qdma_qtype_to_dir(qtype);
}

static int slash_qdma_ioctl_qpair_op_w(struct miscdevice *misc,
                                       struct slash_qdma_dev *qdma_dev,
                                       void __user *uarg)
{
    struct slash_qdma_qpair_op req;
    __u32 user_size = 0;
    size_t copy_size;
    int ret;

    /*
     * First, fetch the size field from userspace so we can
     * safely handle callers built against older or newer
     * versions of the struct.
     */
    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (!user_size || user_size > sizeof(req))
        user_size = sizeof(req);

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, user_size))
        return -EFAULT;

    if (req.op > SLASH_QDMA_QUEUE_OP_DEL)
        return -EINVAL;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    ret = slash_qdma_ioctl_qpair_op(misc, qdma_dev, &req);
    mutex_unlock(&qdma_dev->lock);

    if (ret)
        return ret;

    /*
     * On success, update the size field to reflect the
     * kernel's view of the struct and copy back only as
     * many bytes as the caller originally provided.
     */
    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size))
        return -EFAULT;

    return ret;
}

static int slash_qdma_ioctl_qpair_op(struct miscdevice *misc,
                                     struct slash_qdma_dev *qdma_dev,
                                     struct slash_qdma_qpair_op *req)
{
    struct slash_qdma_qpair_entry *entry;
    int ret = 0;

    (void) misc;

    if (!qdma_dev->have_qdma_handle)
        return -ENODEV;

    entry = slash_qdma_qpair_lookup(qdma_dev, req->qid);
    if (!entry)
        return -ENOENT;

    switch (req->op) {
    case SLASH_QDMA_QUEUE_OP_START:
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          qdma_queue_start,
                                          "start", true);
        break;
    case SLASH_QDMA_QUEUE_OP_STOP:
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          qdma_queue_stop,
                                          "stop", true);
        break;
    case SLASH_QDMA_QUEUE_OP_DEL:
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          qdma_queue_remove,
                                          "remove", false);
        if (!ret)
            slash_qdma_qpair_remove(qdma_dev, req->qid);
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int slash_qdma_ioctl_qpair_op_apply(struct slash_qdma_dev *qdma_dev,
                                           struct slash_qdma_qpair_entry *entry,
                                           struct slash_qdma_qpair_op *req,
                                           slash_qdma_queue_cmd_fn fn,
                                           const char *op_name,
                                           bool stop_on_err)
{
    int idx;
    int first_err = 0;

    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        enum queue_type_t qtype = idx;
        u32 dir_bit = slash_qdma_qtype_to_dir(qtype);
        char errbuf[128] = {0};
        int err;

        if (!(entry->dir_mask & dir_bit) || !entry->qhndl[qtype])
            continue;

        err = fn(qdma_dev->qdma_handle, entry->qhndl[qtype],
                 errbuf, (int)sizeof(errbuf));
        if (err) {
            dev_err(&qdma_dev->pdev->dev,
                    "qdma: queue %s failed (qid=%u, type=%u): %d (%s)\n",
                    op_name, req->qid, qtype, err, errbuf);

            if (stop_on_err)
                return err;

            if (!first_err)
                first_err = err;
        }
    }

    return first_err;
}

static inline void slash_qdma_iocb_release(struct slash_qdma_io_cb *iocb)
{
    if (iocb->pages)
        iocb->pages = NULL;

    kfree(iocb->sgl);
    iocb->sgl = NULL;
    iocb->buf = NULL;
}

static void slash_qdma_unmap_user_buf(struct slash_qdma_io_cb *iocb, bool write)
{
    int i;

    if (!iocb->pages || !iocb->pages_nr)
        return;

    for (i = 0; i < iocb->pages_nr; i++) {
        if (iocb->pages[i]) {
            if (!write)
                set_page_dirty(iocb->pages[i]);
            put_page(iocb->pages[i]);
        } else {
            break;
        }
    }

    if (i != iocb->pages_nr)
        pr_err("slash: qdma: sgl pages %d/%u.\n", i, iocb->pages_nr);

    iocb->pages_nr = 0;
}

static int slash_qdma_map_user_buf_to_sgl(struct slash_qdma_io_cb *iocb,
                                          bool write)
{
    unsigned long len = iocb->len;
    char *buf = (char *)iocb->buf;
    struct qdma_sw_sg *sg;
    unsigned int pg_off = offset_in_page(buf);
    unsigned int pages_nr = (len + pg_off + PAGE_SIZE - 1) >> PAGE_SHIFT;
    int i;
    int rv;

    if (len == 0)
        pages_nr = 1;
    if (pages_nr == 0)
        return -EINVAL;

    iocb->pages_nr = 0;
    sg = kmalloc(pages_nr * (sizeof(struct qdma_sw_sg) +
                             sizeof(struct page *)), GFP_KERNEL);
    if (!sg) {
        pr_err("slash: qdma: sgl allocation failed for %u pages\n",
               pages_nr);
        return -ENOMEM;
    }
    memset(sg, 0, pages_nr * (sizeof(struct qdma_sw_sg) +
                              sizeof(struct page *)));
    iocb->sgl = sg;

    iocb->pages = (struct page **)(sg + pages_nr);
    rv = get_user_pages_fast((unsigned long)buf, pages_nr,
                             1 /* write */, iocb->pages);
    if (rv < 0) {
        pr_err("slash: qdma: unable to pin down %u user pages, %d\n",
               pages_nr, rv);
        goto err_out;
    }
    if (rv != pages_nr) {
        pr_err("slash: qdma: unable to pin down all %u user pages, %d\n",
               pages_nr, rv);
        iocb->pages_nr = rv;
        rv = -EFAULT;
        goto err_out;
    }

    sg = iocb->sgl;
    for (i = 0; i < pages_nr; i++, sg++) {
        unsigned int offset = offset_in_page(buf);
        unsigned int nbytes = min_t(unsigned int,
                                    PAGE_SIZE - offset, len);
        struct page *pg = iocb->pages[i];

        flush_dcache_page(pg);

        sg->next = sg + 1;
        sg->pg = pg;
        sg->offset = offset;
        sg->len = nbytes;
        sg->dma_addr = 0UL;

        buf += nbytes;
        len -= nbytes;
    }

    iocb->sgl[pages_nr - 1].next = NULL;
    iocb->pages_nr = pages_nr;
    return 0;

err_out:
    slash_qdma_unmap_user_buf(iocb, write);
    slash_qdma_iocb_release(iocb);

    return rv;
}

static ssize_t slash_qdma_qpair_read_write(struct file *file, char __user *buf,
                                           size_t count, loff_t *ppos,
                                           bool write)
{
    struct slash_qdma_qpair_file_ctx *ctx = file->private_data;
    struct slash_qdma_dev *qdma_dev;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_io_cb iocb;
    struct qdma_request *req;
    unsigned long qhndl;
    ssize_t res;
    int rv;

    if (!ctx)
        return -EINVAL;

    qdma_dev = ctx->qdma_dev;
    entry = ctx->entry;

    if (!qdma_dev || !entry)
        return -ENODEV;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }

    if (write) {
        if (!(entry->dir_mask & SLASH_QDMA_DIR_H2C) ||
            !entry->qhndl[Q_H2C]) {
            mutex_unlock(&qdma_dev->lock);
            return -ENODEV;
        }
        qhndl = entry->qhndl[Q_H2C];
    } else {
        if (!(entry->dir_mask & SLASH_QDMA_DIR_C2H) ||
            !entry->qhndl[Q_C2H]) {
            mutex_unlock(&qdma_dev->lock);
            return -ENODEV;
        }
        qhndl = entry->qhndl[Q_C2H];
    }
    mutex_unlock(&qdma_dev->lock);

    memset(&iocb, 0, sizeof(iocb));
    iocb.buf = buf;
    iocb.len = count;
    rv = slash_qdma_map_user_buf_to_sgl(&iocb, write);
    if (rv < 0)
        return rv;

    req = &iocb.req;
    req->sgcnt = iocb.pages_nr;
    req->sgl = iocb.sgl;
    req->write = write ? 1 : 0;
    req->dma_mapped = 0;
    req->udd_len = 0;
    req->ep_addr = (u64)*ppos;
    req->count = count;
    req->timeout_ms = 10 * 1000;
    req->fp_done = NULL;
    req->h2c_eot = 1;

    res = qdma_request_submit(qdma_dev->qdma_handle, qhndl, req);
    if (res > 0)
        *ppos += res;

    slash_qdma_unmap_user_buf(&iocb, write);
    slash_qdma_iocb_release(&iocb);

    return res;
}

static ssize_t slash_qdma_qpair_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos)
{
    return slash_qdma_qpair_read_write(file, buf, count, ppos, false);
}

static ssize_t slash_qdma_qpair_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos)
{
    return slash_qdma_qpair_read_write(file, (char __user *)buf,
                                       count, ppos, true);
}

static long slash_qdma_qpair_ioctl(struct file *file,
                                   unsigned int cmd, unsigned long arg)
{
    (void)file;
    (void)cmd;
    (void)arg;

    return -ENOTTY;
}

static int slash_qdma_qpair_release(struct inode *inode, struct file *file)
{
    struct slash_qdma_qpair_file_ctx *ctx = file->private_data;

    (void)inode;

    if (ctx) {
        if (ctx->entry)
            slash_qdma_qpair_put(ctx->entry);
        if (ctx->qdma_dev)
            kref_put(&ctx->qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        file->private_data = NULL;
    }

    return 0;
}

static int slash_qdma_ioctl_qpair_get_fd_w(struct miscdevice *misc,
                                           struct slash_qdma_dev *qdma_dev,
                                           void __user *uarg)
{
    struct slash_qdma_qpair_fd_request req;
    __u32 user_size = 0;
    size_t copy_size;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_qpair_file_ctx *ctx;
    struct file *file;
    int fd;
    int err;

    (void)misc;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (!user_size || user_size > sizeof(req))
        user_size = sizeof(req);

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, user_size))
        return -EFAULT;

    if (req.flags & ~O_CLOEXEC)
        return -EINVAL;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }

    entry = slash_qdma_qpair_lookup(qdma_dev, req.qid);
    if (!entry || !entry->dir_mask) {
        mutex_unlock(&qdma_dev->lock);
        return -ENOENT;
    }

    slash_qdma_qpair_get(entry);
    kref_get(&qdma_dev->ref);
    mutex_unlock(&qdma_dev->lock);

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        slash_qdma_qpair_put(entry);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        return -ENOMEM;
    }

    ctx->qdma_dev = qdma_dev;
    ctx->entry = entry;
    ctx->qid = req.qid;

    file = anon_inode_getfile("slash_qdma_qpair", &slash_qdma_qpair_fops,
                              ctx, O_RDWR | (req.flags & O_CLOEXEC));
    if (IS_ERR(file)) {
        err = PTR_ERR(file);
        slash_qdma_qpair_put(entry);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        return err;
    }

    fd = get_unused_fd_flags(req.flags & O_CLOEXEC);
    if (fd < 0) {
        fput(file);
        slash_qdma_qpair_put(entry);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        return fd;
    }

    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size)) {
        put_unused_fd(fd);
        fput(file);
        slash_qdma_qpair_put(entry);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        return -EFAULT;
    }

    fd_install(fd, file);

    return fd;
}

/* Must be called with qdma_dev->lock held */
static void slash_qdma_qpair_teardown(struct slash_qdma_dev *qdma_dev, u32 qid,
                                      struct slash_qdma_qpair_entry *entry)
{
    unsigned int idx;

    if (!entry)
        return;

    /* Remove any queues that still exist */
    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        enum queue_type_t qtype = idx;

        if (entry->dir_mask & slash_qdma_qtype_to_dir(qtype))
            slash_qdma_ioctl_qpair_rm_q(&qdma_dev->misc, qdma_dev, entry, qtype);
    }

    /* Mark entry dead for any stale FDs */
    memset(entry->qhndl, 0, sizeof(entry->qhndl));
    entry->dir_mask = 0;

    /* Drop from xarray and release ref */
    xa_erase(&qdma_dev->qpairs, qid);
    slash_qdma_qpair_put(entry);
}
