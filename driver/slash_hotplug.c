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

#include "slash_hotplug_driver.h"

#include "slash.h"

#include <slash/uapi/slash_hotplug.h>

#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define SLASH_HOTPLUG_MODE 0600

struct slash_hotplug_entry {
    struct list_head node;
    char bdf[SLASH_HOTPLUG_BDF_LEN];
};

static DEFINE_MUTEX(slash_hotplug_devices_lock);
static LIST_HEAD(slash_hotplug_devices);
static unsigned int slash_hotplug_device_count;

static struct slash_hotplug_entry *slash_hotplug_find_entry_locked(const char *bdf)
{
    struct slash_hotplug_entry *entry;

    list_for_each_entry(entry, &slash_hotplug_devices, node) {
        if (!strcmp(entry->bdf, bdf))
            return entry;
    }

    return NULL;
}

static int slash_hotplug_copy_request(unsigned long arg, struct slash_hotplug_device_request *req)
{
    if (copy_from_user(req, (void __user *)arg, sizeof(*req)))
        return -EFAULT;

    if (req->size && req->size < sizeof(*req))
        return -EINVAL;

    if (!req->size)
        req->size = sizeof(*req);

    req->bdf[SLASH_HOTPLUG_BDF_LEN - 1] = '\0';
    strim(req->bdf);

    return 0;
}

static int slash_hotplug_resolve_request_locked(struct slash_hotplug_device_request *req, bool allow_default)
{
    struct slash_hotplug_entry *entry;

    if (!req->bdf[0]) {
        if (!allow_default)
            return -EINVAL;

        if (slash_hotplug_device_count == 0)
            return -ENODEV;

        if (slash_hotplug_device_count > 1)
            return -EOPNOTSUPP;

        entry = list_first_entry(&slash_hotplug_devices, struct slash_hotplug_entry, node);
        strscpy(req->bdf, entry->bdf, sizeof(req->bdf));
        return 0;
    }

    entry = slash_hotplug_find_entry_locked(req->bdf);
    if (!entry)
        return -ENODEV;

    return 0;
}

static int slash_hotplug_get_pci_dev(const char *bdf, struct pci_dev **pdev_out)
{
    int domain, bus, slot, func;
    struct pci_dev *pdev;

    if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4)
        return -EINVAL;

    pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, func));
    if (!pdev)
        return -ENODEV;

    *pdev_out = pdev;
    return 0;
}

static int slash_hotplug_handle_rescan(void)
{
    struct pci_bus *bus;

    list_for_each_entry(bus, &pci_root_buses, node)
        pci_rescan_bus(bus);

    return 0;
}

static int slash_hotplug_handle_remove(const char *bdf)
{
    struct pci_dev *pdev;
    int ret = slash_hotplug_get_pci_dev(bdf, &pdev);

    if (ret) {
        pr_err("slash_hotplug: remove: BDF %s unavailable (%d)\n", bdf, ret);
        return ret;
    }

    pr_info("slash_hotplug: removing %s\n", pci_name(pdev));
    pci_stop_and_remove_bus_device(pdev);
    pci_dev_put(pdev);

    return 0;
}

static int slash_hotplug_handle_toggle_sbr(const char *bdf)
{
    struct pci_dev *pdev;
    struct pci_dev *root;
    int ret;
    u16 ctrl;

    ret = slash_hotplug_get_pci_dev(bdf, &pdev);
    if (ret) {
        pr_err("slash_hotplug: toggle_sbr: BDF %s unavailable (%d)\n", bdf, ret);
        return ret;
    }

    root = pcie_find_root_port(pdev);
    if (!root) {
        pr_err("slash_hotplug: toggle_sbr: no root port for %s\n", pci_name(pdev));
        pci_dev_put(pdev);
        return -ENODEV;
    }

    pci_dev_get(root);

    ret = pci_read_config_word(root, PCI_BRIDGE_CONTROL, &ctrl);
    if (ret) {
        pr_err("slash_hotplug: toggle_sbr: read control failed (%d)\n", ret);
        goto out_put;
    }

    ret = pci_write_config_word(root, PCI_BRIDGE_CONTROL, ctrl | PCI_BRIDGE_CTL_BUS_RESET);
    if (ret) {
        pr_err("slash_hotplug: toggle_sbr: assert SBR failed (%d)\n", ret);
        goto out_put;
    }

    msleep(2);

    ret = pci_write_config_word(root, PCI_BRIDGE_CONTROL, ctrl & ~PCI_BRIDGE_CTL_BUS_RESET);
    if (ret)
        pr_err("slash_hotplug: toggle_sbr: deassert SBR failed (%d)\n", ret);
    else
        msleep(5000);

out_put:
    pci_dev_put(root);
    pci_dev_put(pdev);
    return ret;
}

static int slash_hotplug_handle_hotplug(const char *bdf)
{
    struct pci_dev *pdev;
    struct pci_dev *root;
    struct pci_bus *bus;
    int ret;

    ret = slash_hotplug_get_pci_dev(bdf, &pdev);
    if (ret) {
        pr_err("slash_hotplug: hotplug: BDF %s unavailable (%d)\n", bdf, ret);
        return ret;
    }

    root = pcie_find_root_port(pdev);
    if (!root) {
        pr_err("slash_hotplug: hotplug: no root port for %s\n", pci_name(pdev));
        pci_dev_put(pdev);
        return -ENODEV;
    }

    pci_dev_get(root);
    bus = root->subordinate;
    if (!bus) {
        pr_err("slash_hotplug: hotplug: root port has no subordinate bus\n");
        ret = -ENODEV;
        goto out_put_root;
    }

    dev_info(&pdev->dev, "slash_hotplug: removing device for hotplug cycle\n");
    pci_stop_and_remove_bus_device(pdev);
    pci_dev_put(pdev);

    pci_rescan_bus(bus);
    ret = 0;

out_put_root:
    pci_dev_put(root);
    return ret;
}

static long slash_hotplug_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct slash_hotplug_device_request req = {0};
    int ret;

    switch (cmd) {
    case SLASH_HOTPLUG_IOCTL_RESCAN:
        ret = slash_hotplug_handle_rescan();
        break;
    case SLASH_HOTPLUG_IOCTL_REMOVE:
        ret = slash_hotplug_copy_request(arg, &req);
        if (ret)
            break;
        mutex_lock(&slash_hotplug_devices_lock);
        ret = slash_hotplug_resolve_request_locked(&req, true);
        mutex_unlock(&slash_hotplug_devices_lock);
        if (!ret)
            ret = slash_hotplug_handle_remove(req.bdf);
        break;
    case SLASH_HOTPLUG_IOCTL_TOGGLE_SBR:
        ret = slash_hotplug_copy_request(arg, &req);
        if (ret)
            break;
        mutex_lock(&slash_hotplug_devices_lock);
        ret = slash_hotplug_resolve_request_locked(&req, true);
        mutex_unlock(&slash_hotplug_devices_lock);
        if (!ret)
            ret = slash_hotplug_handle_toggle_sbr(req.bdf);
        break;
    case SLASH_HOTPLUG_IOCTL_HOTPLUG:
        ret = slash_hotplug_copy_request(arg, &req);
        if (ret)
            break;
        mutex_lock(&slash_hotplug_devices_lock);
        ret = slash_hotplug_resolve_request_locked(&req, true);
        mutex_unlock(&slash_hotplug_devices_lock);
        if (!ret)
            ret = slash_hotplug_handle_hotplug(req.bdf);
        break;
    default:
        ret = -ENOTTY;
        break;
    }

    if (ret == -EOPNOTSUPP)
        pr_err("slash_hotplug: multiple devices tracked; specify BDF explicitly\n");

    return ret;
}

#ifdef CONFIG_COMPAT
static long slash_hotplug_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    return slash_hotplug_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations slash_hotplug_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = slash_hotplug_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = slash_hotplug_compat_ioctl,
#endif
};

static struct miscdevice slash_hotplug_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = SLASH_HOTPLUG_DEVICE_NAME,
    .fops = &slash_hotplug_fops,
    .mode = SLASH_HOTPLUG_MODE,
};

int slash_hotplug_register_device(struct pci_dev *pdev)
{
    struct slash_hotplug_entry *entry;
    char bdf[SLASH_HOTPLUG_BDF_LEN];

    snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%x",
             pci_domain_nr(pdev->bus), pdev->bus->number,
             PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;

    strscpy(entry->bdf, bdf, sizeof(entry->bdf));

    mutex_lock(&slash_hotplug_devices_lock);
    if (slash_hotplug_find_entry_locked(entry->bdf)) {
        mutex_unlock(&slash_hotplug_devices_lock);
        kfree(entry);
        return 0;
    }

    list_add_tail(&entry->node, &slash_hotplug_devices);
    slash_hotplug_device_count++;
    mutex_unlock(&slash_hotplug_devices_lock);

    dev_info(&pdev->dev, "slash_hotplug: tracking device %s\n", entry->bdf);

    return 0;
}

void slash_hotplug_unregister_device(struct pci_dev *pdev)
{
    struct slash_hotplug_entry *entry;
    char bdf[SLASH_HOTPLUG_BDF_LEN];

    snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%x",
             pci_domain_nr(pdev->bus), pdev->bus->number,
             PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    mutex_lock(&slash_hotplug_devices_lock);
    entry = slash_hotplug_find_entry_locked(bdf);
    if (entry) {
        list_del(&entry->node);
        if (slash_hotplug_device_count)
            slash_hotplug_device_count--;
    }
    mutex_unlock(&slash_hotplug_devices_lock);

    if (entry) {
        dev_info(&pdev->dev, "slash_hotplug: untracked device %s\n", bdf);
        kfree(entry);
    } else {
        dev_dbg(&pdev->dev, "slash_hotplug: device %s not tracked\n", bdf);
    }
}

int slash_hotplug_init(void)
{
    int ret;

    pr_info("slash_hotplug: registering misc device\n");

    ret = misc_register(&slash_hotplug_misc);
    if (ret) {
        pr_err("slash_hotplug: misc_register failed: %d\n", ret);
        return ret;
    }

    return 0;
}

void slash_hotplug_exit(void)
{
    struct slash_hotplug_entry *entry, *tmp;

    mutex_lock(&slash_hotplug_devices_lock);
    list_for_each_entry_safe(entry, tmp, &slash_hotplug_devices, node) {
        list_del(&entry->node);
        kfree(entry);
    }
    slash_hotplug_device_count = 0;
    mutex_unlock(&slash_hotplug_devices_lock);

    misc_deregister(&slash_hotplug_misc);
    pr_info("slash_hotplug: misc device unregistered\n");
}
