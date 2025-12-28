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

#ifndef SLASH_HOTPLUG_DRIVER_H
#define SLASH_HOTPLUG_DRIVER_H

#include <linux/pci.h>

int slash_hotplug_init(void);
void slash_hotplug_exit(void);
int slash_hotplug_register_device(struct pci_dev *pdev);
void slash_hotplug_unregister_device(struct pci_dev *pdev);

#endif /* SLASH_HOTPLUG_DRIVER_H */
