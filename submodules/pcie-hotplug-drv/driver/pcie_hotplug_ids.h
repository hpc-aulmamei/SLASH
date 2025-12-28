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

#ifndef PCIE_HOTPLUG_IDS
#define PCIE_HOTPLUG_IDS

#include <linux/pci.h>

/**
 * @file pcie_hotplug_ids.h
 * @brief Defines a list of PCI device IDs for the PCIe hotplug driver.
 *
 * This header file contains a list of PCI device IDs that the PCIe hotplug driver
 * will recognize and handle. The list is defined using the `pci_device_id` structure
 * and the `PCI_DEVICE` macro.
 */

/**
 * @brief List of PCI device IDs supported by the PCIe hotplug driver.
 *
 * This list contains the vendor ID and device ID pairs for the PCI devices that
 * the PCIe hotplug driver will recognize and handle. The list is terminated by
 * an entry with all fields set to 0.
 */
static const struct pci_device_id pcie_hotplug_ids[] = {
    {PCI_DEVICE(0x10ee, 0x50b4)}, /**< Xilinx V80 PCIe device */
    {
        0,
    } /**< End of list */
};


#endif  // PCIE_HOTPLUG_IDS