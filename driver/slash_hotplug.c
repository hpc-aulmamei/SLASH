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

/**
 * @file slash_hotplug.c
 *
 * PCIe hot-plug and reset subsystem for the SLASH kernel module.
 *
 * This file manages the PCIe-level lifecycle of SLASH FPGA devices,
 * providing four operations via /dev/slash_hotplug:
 *
 *   - **RESCAN**     — rescan all PCI root buses to discover new devices.
 *   - **REMOVE**     — remove a specific device from the PCI bus.
 *   - **TOGGLE_SBR** — assert and deassert a Secondary Bus Reset on
 *                      the device's upstream root port.
 *   - **HOTPLUG**    — atomic remove + rescan cycle on the root port's
 *                      subordinate bus.
 *
 * These operations are essential for FPGA reconfiguration workflows.
 * When a new bitstream is loaded, the FPGA's PCI identity and BAR
 * layout may change, requiring the device to be removed from the bus,
 * reset via SBR, and re-enumerated.
 *
 * A typical reconfiguration flow:
 *   1. REMOVE each PCI function (PF0, PF1, PF2 ...)
 *   2. TOGGLE_SBR to reset the device
 *   3. (the 5 s settle in SBR allows the FPGA to re-initialize)
 *   4. RESCAN to discover the new configuration
 *
 * Device tracking:
 *   The driver maintains a linked list of tracked devices (registered
 *   during PCI probe, unregistered during PCI remove).  Ioctls can
 *   either specify a BDF explicitly or omit it to target the only
 *   tracked device (a convenience for single-card systems).
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

/**
 * struct slash_hotplug_entry - A tracked SLASH device.
 * @node: Linked list linkage into slash_hotplug_devices.
 * @bdf:  PCI Bus/Device/Function string (e.g. "0000:03:00.2").
 *
 * One entry exists per PCI function that has been probed and
 * registered via slash_hotplug_register_device().
 */
struct slash_hotplug_entry {
    struct list_head node;
    char bdf[SLASH_HOTPLUG_BDF_LEN];
};

/*
 * Global tracking list.  Protected by slash_hotplug_devices_lock.
 * slash_hotplug_device_count is maintained alongside the list for
 * O(1) checks in the default-device resolution path.
 */
static DEFINE_MUTEX(slash_hotplug_devices_lock);
static LIST_HEAD(slash_hotplug_devices);
static unsigned int slash_hotplug_device_count;

/**
 * slash_hotplug_find_entry_locked() - Look up a tracked device by BDF.
 * @bdf: BDF string to search for.
 *
 * Caller must hold slash_hotplug_devices_lock.
 *
 * Return: Pointer to the matching entry, or NULL if not found.
 */
static struct slash_hotplug_entry *slash_hotplug_find_entry_locked(const char *bdf)
{
    struct slash_hotplug_entry *entry;

    list_for_each_entry(entry, &slash_hotplug_devices, node) {
        if (!strcmp(entry->bdf, bdf))
            return entry;
    }

    return NULL;
}

/**
 * slash_hotplug_copy_request() - Copy and sanitize a hotplug request from userspace.
 * @arg: Userspace pointer to the request struct.
 * @req: Kernel-side buffer to populate.
 *
 * NUL-terminates and trims whitespace from the BDF string.
 *
 * Return: 0 on success, -EFAULT or -EINVAL on failure.
 */
static int slash_hotplug_copy_request(unsigned long arg, struct slash_hotplug_device_request *req)
{
    if (copy_from_user(req, (void __user *)arg, sizeof(*req)))
        return -EFAULT;

    if (req->size && req->size < sizeof(*req))
        return -EINVAL;

    if (!req->size)
        req->size = sizeof(*req);

    /* Defend against unterminated strings from userspace. */
    req->bdf[SLASH_HOTPLUG_BDF_LEN - 1] = '\0';
    strim(req->bdf);

    return 0;
}

/**
 * slash_hotplug_resolve_request_locked() - Fill in the BDF if not provided.
 * @req:           Request whose @bdf may be empty.
 * @allow_default: If true and @bdf is empty, auto-fill from the only
 *                 tracked device.
 *
 * When the caller omits the BDF (empty string), this function provides
 * a "default device" convenience:
 *   - 0 devices tracked → -ENODEV
 *   - 1 device tracked  → auto-fill @bdf from it
 *   - >1 devices tracked → -EOPNOTSUPP (ambiguous; caller must specify)
 *
 * When a BDF is provided, it is validated against the tracking list.
 *
 * Caller must hold slash_hotplug_devices_lock.
 *
 * Return: 0 on success, negative errno on failure.
 */
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

/**
 * slash_hotplug_get_pci_dev() - Look up a PCI device by BDF string.
 * @bdf:      BDF string in "DDDD:BB:SS.F" hex format.
 * @pdev_out: On success, receives a reference-counted pci_dev pointer.
 *            Caller must call pci_dev_put() when done.
 *
 * Return: 0 on success, -EINVAL if the BDF is malformed, -ENODEV if
 *         the device is not present.
 */
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

/**
 * slash_hotplug_handle_rescan() - Rescan all PCI root buses.
 *
 * Discovers any new or reconfigured devices on every root bus.
 *
 * Return: Always 0.
 */
static int slash_hotplug_handle_rescan(void)
{
    struct pci_bus *bus;

    list_for_each_entry(bus, &pci_root_buses, node)
        pci_rescan_bus(bus);

    return 0;
}

/**
 * slash_hotplug_handle_remove() - Remove a device from the PCI bus.
 * @bdf: BDF string identifying the device to remove.
 *
 * Stops the device, tears down its driver bindings, and removes it
 * from the PCI hierarchy.  The device can be re-discovered later via
 * a bus rescan.
 *
 * Return: 0 on success, negative errno on failure.
 */
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

/**
 * slash_hotplug_handle_toggle_sbr() - Perform a Secondary Bus Reset.
 * @bdf: BDF string identifying the device (or its former location).
 *
 * Locates the upstream root port for the given BDF and toggles the
 * PCI_BRIDGE_CTL_BUS_RESET bit in the bridge's PCI_BRIDGE_CONTROL
 * register.  The sequence is:
 *
 *   1. Read the current bridge control register.
 *   2. Assert SBR (set the BUS_RESET bit).
 *   3. Wait 2 ms — the PCIe spec minimum reset hold time.
 *   4. Deassert SBR (clear the BUS_RESET bit).
 *   5. Wait 5 s — empirically determined settle time for the V80 FPGA
 *      to complete bitstream loading and bring up its PCI endpoints.
 *
 * Bridge resolution strategy:
 *   The endpoint may have already been removed from the PCI hierarchy
 *   (e.g. by a prior REMOVE ioctl).  In that case, the endpoint's
 *   pci_dev no longer exists, but the bus structure and its bridge
 *   device survive.  We try the endpoint first (pcie_find_root_port),
 *   then fall back to the bus (pci_find_bus → bus->self).
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_hotplug_handle_toggle_sbr(const char *bdf)
{
    struct pci_dev *pdev;
    struct pci_dev *bridge = NULL;
    int domain, bus_nr, slot, func;
    int ret;
    u16 ctrl;

    if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus_nr, &slot, &func) != 4)
        return -EINVAL;

    /*
     * Try to resolve the upstream bridge from the endpoint.  If the
     * endpoint has already been removed (e.g. by a prior hotplug remove),
     * fall back to locating the bridge via the bus number, which survives
     * endpoint removal.
     */
    pdev = pci_get_domain_bus_and_slot(domain, bus_nr, PCI_DEVFN(slot, func));
    if (pdev) {
        struct pci_dev *root = pcie_find_root_port(pdev);

        if (root)
            bridge = pci_dev_get(root);
        pci_dev_put(pdev);
    }

    /* Fallback: locate bridge via the bus topology directly. */
    if (!bridge) {
        struct pci_bus *ep_bus = pci_find_bus(domain, bus_nr);

        if (ep_bus && ep_bus->self)
            bridge = pci_dev_get(ep_bus->self);
    }

    if (!bridge) {
        pr_err("slash_hotplug: toggle_sbr: no upstream bridge for %s\n", bdf);
        return -ENODEV;
    }

    /* Read current bridge control state so we can restore it after reset. */
    ret = pci_read_config_word(bridge, PCI_BRIDGE_CONTROL, &ctrl);
    if (ret) {
        pr_err("slash_hotplug: toggle_sbr: read control failed (%d)\n", ret);
        goto out_put;
    }

    /* Assert SBR. */
    ret = pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl | PCI_BRIDGE_CTL_BUS_RESET);
    if (ret) {
        pr_err("slash_hotplug: toggle_sbr: assert SBR failed (%d)\n", ret);
        goto out_put;
    }

    /* PCIe spec requires at least 1 ms reset hold; we use 2 ms for margin. */
    msleep(2);

    /* Deassert SBR and wait for the FPGA to re-initialize. */
    ret = pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl & ~PCI_BRIDGE_CTL_BUS_RESET);
    if (ret)
        pr_err("slash_hotplug: toggle_sbr: deassert SBR failed (%d)\n", ret);
    else
        /* 5 s settle: the V80 FPGA needs this long to reload and bring up endpoints. */
        msleep(5000);

out_put:
    pci_dev_put(bridge);
    return ret;
}

/**
 * slash_hotplug_handle_hotplug() - Perform a full hot-plug cycle.
 * @bdf: BDF string identifying the device.
 *
 * Removes the device from the bus, then rescans the root port's
 * subordinate bus to re-enumerate it.  This is an atomic
 * remove-then-rescan, useful when the device identity hasn't changed
 * but the kernel needs to rebind drivers.
 *
 * Note: this does **not** include an SBR.  If the FPGA bitstream has
 * changed and a reset is needed, call TOGGLE_SBR separately before
 * HOTPLUG.
 *
 * Return: 0 on success, negative errno on failure.
 */
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

    /* Rescan the same bus subtree to re-discover the device. */
    pci_rescan_bus(bus);
    ret = 0;

out_put_root:
    pci_dev_put(root);
    return ret;
}

/**
 * slash_hotplug_ioctl() - Dispatch hotplug ioctl commands.
 * @file: Open file for the hotplug misc device.
 * @cmd:  ioctl command number.
 * @arg:  Userspace pointer to the request struct (for commands that need one).
 *
 * Return: 0 on success, negative errno on failure.
 */
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
        /*
         * Skip the tracked-device check when an explicit BDF is provided.
         * toggle_sbr is normally called after the endpoint has already been
         * removed, so it will no longer appear in the tracking list.  The
         * handler itself resolves the upstream bridge from the bus topology,
         * which survives endpoint removal.
         *
         * When no BDF is given, fall back to the resolver to pick the
         * single tracked device (the usual default-device behaviour).
         */
        if (!req.bdf[0]) {
            mutex_lock(&slash_hotplug_devices_lock);
            ret = slash_hotplug_resolve_request_locked(&req, true);
            mutex_unlock(&slash_hotplug_devices_lock);
            if (ret)
                break;
        }
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
/**
 * slash_hotplug_compat_ioctl() - Handle 32-bit compat ioctls.
 *
 * Converts the 32-bit userspace pointer to a native pointer and
 * delegates to the standard ioctl handler.  The request struct is
 * the same size on 32-bit and 64-bit, so no field translation is
 * needed.
 */
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
        /* Already tracked — silently deduplicate. */
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

/**
 * slash_hotplug_exit() - Tear down the hotplug subsystem.
 *
 * Frees all tracked device entries and deregisters the misc device.
 */
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
