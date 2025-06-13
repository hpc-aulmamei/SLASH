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

#ifndef PCIE_HOTPLUG_H
#define PCIE_HOTPLUG_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/ioctl.h>

#include "pcie_hotplug_ids.h"

#define PCIE_IOCTL_MAGIC 0xB5

#define PCIE_IOCTL_RESCAN      _IO(PCIE_IOCTL_MAGIC, 0x01)
#define PCIE_IOCTL_REMOVE      _IO(PCIE_IOCTL_MAGIC, 0x02)
#define PCIE_IOCTL_TOGGLE_SBR  _IO(PCIE_IOCTL_MAGIC, 0x03)
#define PCIE_IOCTL_HOTPLUG     _IO(PCIE_IOCTL_MAGIC, 0x04)


struct pcie_bar_read {
    unsigned int bar_index;       // IN: BAR number (0-5)
    unsigned int offset;          // IN: Offset in BAR to read
    u32 value;                    // OUT: Value read (32-bit)
};

#define PCIE_IOCTL_GET_BAR_VAL _IOWR(PCIE_IOCTL_MAGIC, 0x05, struct pcie_bar_read)

static int major_number;
static struct class* pcie_hotplug_class = NULL;

struct pcie_hotplug_device {
    char *bdf;
    char rootport_bdf[32];
    dev_t devt;
    struct cdev cdev;
    struct list_head list;
};

static LIST_HEAD(device_list);
static int device_count = 0;

    /**
     * Helper Functions
     */

    /**
     * get_pci_dev_by_bdf - Get PCI device by BDF
     * @bdf: BDF string
     *
     * Returns the PCI device corresponding to the given BDF.
     */
    static struct pci_dev *get_pci_dev_by_bdf(const char* bdf);

    /**
     * get_bdfs - Get BDFs for the device and root port
     *
     * Returns 0 on success, negative error code on failure.
     */
    static int get_bdfs(const char *device_bdf, char *rootport_bdf);

    /**
     * get_next_function_pci_dev - Get next function PCIe device
     * Returns the PCIe device corresponding to the given BDF.
     */
    static struct pci_dev *get_next_function_pci_dev(const char* bdf);

    /**
     * toggle_sbr - Toggle Secondary Bus Reset (SBR)
     *
     * Toggles the SBR for the given device.
     */
    static void toggle_sbr(struct pcie_hotplug_device *dev);

    /**
     * handle_rescan - Handle PCIe bus rescan
     *
     * Rescans the PCIe bus.
     */
    static void handle_rescan(void);

    /**
     * handle_pcie_remove - Handle PCIe device removal
     *
     * Removes the given PCIe device.
     */
    static void handle_pcie_remove(struct pcie_hotplug_device *dev);

    /**
     * handle_pcie_hotplug - Handle PCIe hotplug
     *
     * Handles the hotplug operation for the given device.
     */
    static void handle_pcie_hotplug(struct pcie_hotplug_device *dev);

    /**
     * Kernel Specific Functions
     */

    static long pcie_hotplug_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

    static int pcie_hotplug_probe(struct pci_dev *pdev, const struct pci_device_id *id);

    static void pcie_hotplug_remove(struct pci_dev *pdev);

    // // /**
    // //  * @brief Store function for adding a new PCIe hotplug device.
    // //  *
    // //  * This function is called when a new device BDF is written to the sysfs entry.
    // //  * It allocates and initializes a new `pcie_hotplug_device` structure, finds the
    // //  * root port for the device, allocates a character device region, and creates
    // //  * the character device.
    // //  *
    // //  * @param kobj Pointer to the kobject.
    // //  * @param attr Pointer to the kobj_attribute.
    // //  * @param buf Buffer containing the BDF string.
    // //  * @param count Number of bytes in the buffer.
    // //  * @return Number of bytes processed on success, negative error code on failure.
    // //  */
    // // static ssize_t add_device_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);

    // static void add_device(const char *bdf);

    /**
     * pcie_hotplug_init - Module initialization function
     *
     * Initializes the PCIe hotplug module.
     *
     * Returns 0 on success, negative error code on failure.
     */
    static int __init pcie_hotplug_init(void);

    /**
     * pcie_hotplug_exit - Module exit function
     *
     * Cleans up the PCIe hotplug module.
     */
    static void __exit pcie_hotplug_exit(void);

#endif // PCIE_HOTPLUG_H