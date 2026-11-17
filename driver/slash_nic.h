/**
 * Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
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
 * @file slash_nic.h
 *
 * Network devices for the SLASH NIC RM (nic_core: two Ethernet ports, QSFP0
 * and QSFP2, behind PF2 BAR2; frames through PF1's QDMA ST queues).
 *
 * A card's netdevs exist only while PF1 and PF2 are both bound and the user
 * attached the NIC (`echo 1 > /sys/bus/pci/devices/<PF2>/nic`) after the NIC
 * RM identified itself. Removing either function (any RM reload) detaches.
 * While attached, the netdevs own PF1 qpair ids 248..251 (H2C 248 + port,
 * C2H 250 + port).
 */

#ifndef SLASH_NIC_H
#define SLASH_NIC_H

#include <linux/pci.h>

/** PF1 probed and its libqdma device is open; @board is the shared card number. */
void slash_nic_qdma_add(struct pci_dev *pdev, unsigned long qdma_handle, int board);
/** PF1 is going away: detach first (before qdma_device_close()). */
void slash_nic_qdma_remove(struct pci_dev *pdev);
/** PF2 probed; creates its sysfs `nic` attribute. */
void slash_nic_ctl_add(struct pci_dev *pdev, int board);
/** PF2 is going away: removes the attribute and detaches (before the BARs go). */
void slash_nic_ctl_remove(struct pci_dev *pdev);

/*
 * In slash_qdma.c, for the netdev queues: reserve / release qpair ids
 * @first .. @first + @n - 1 of PF1 so user space can neither allocate nor
 * operate on them. reserve: 0, -EBUSY (an id is in use), -ENODEV.
 */
int slash_qdma_reserve_qids(struct pci_dev *pdev, u32 first, u32 n);
void slash_qdma_release_qids(struct pci_dev *pdev, u32 first, u32 n);

#endif /* SLASH_NIC_H */
