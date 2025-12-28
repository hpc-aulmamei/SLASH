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
#ifndef SLASH_CONFIG_H
#define SLASH_CONFIG_H

#define SLASH_PCIE_VENDOR_ID 0x10EE
#define SLASH_PCIE_DEVICE_ID 0x50B6
#define SLASH_PCIE_PF 2
// subsystem id e

#define SLASH_NAME "slash"

#define SLASH_CTLDEV_NAME_FMT "slash_ctl_%s" /* uses pci_name, appears in /sys/class/misc */
#define SLASH_CTLDEV_NODENAME_FMT "slash/ctl%d" /* uses an incrementing variable, appears in /dev */
#define SLASH_QDMA_CTLDEV_NAME_FMT "slash_qdma_ctl_%s"
#define SLASH_QDMA_CTLDEV_NODENAME_FMT "slash/qdma_ctl%d"

/* Mode of /dev device, prefer using an udev rule instead of changing this */
#define SLASH_CTLDEV_MODE 0600
#define SLASH_CTLDEV_QDMA_MODE 0600

#undef pr_fmt
#define pr_fmt(fmt) "%s:%s: " fmt, SLASH_NAME, __func__

#endif /* SLASH_CONFIG_H */
