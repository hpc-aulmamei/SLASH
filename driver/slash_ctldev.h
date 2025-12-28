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

#ifndef SLASH_CTLDEV_H
#define SLASH_CTLDEV_H

#include <linux/dma-buf.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>

struct slash_ctldev_bar {
    unsigned int active : 1;
    unsigned int mmio : 1;
    resource_size_t start;
    resource_size_t end;
    resource_size_t len;

    struct dma_buf *dmabuf;
};

struct slash_ctldev {
    struct pci_dev *pdev;
    struct miscdevice misc;
    struct slash_ctldev_bar bars[PCI_STD_NUM_BARS];
};

int slash_ctldev_create(struct pci_dev *pdev);
void slash_ctldev_destroy(struct pci_dev *pdev);

#endif /* SLASH_CTLDEV_H */
