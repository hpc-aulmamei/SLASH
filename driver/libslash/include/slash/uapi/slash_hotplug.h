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

#ifndef SLASH_HOTPLUG_UAPI_H
#define SLASH_HOTPLUG_UAPI_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

#define SLASH_HOTPLUG_DEVICE_NAME "slash_hotplug"
#define SLASH_HOTPLUG_BDF_LEN 32

struct slash_hotplug_device_request {
    __u32 size;
    char bdf[SLASH_HOTPLUG_BDF_LEN]; /* Optional: empty string targets the only tracked device */
};

#define SLASH_HOTPLUG_IOCTL_MAGIC 'w'

#define SLASH_HOTPLUG_IOCTL_RESCAN     _IO(SLASH_HOTPLUG_IOCTL_MAGIC, 0x30)
#define SLASH_HOTPLUG_IOCTL_REMOVE     _IOW(SLASH_HOTPLUG_IOCTL_MAGIC, 0x31, struct slash_hotplug_device_request)
#define SLASH_HOTPLUG_IOCTL_TOGGLE_SBR _IOW(SLASH_HOTPLUG_IOCTL_MAGIC, 0x32, struct slash_hotplug_device_request)
#define SLASH_HOTPLUG_IOCTL_HOTPLUG    _IOW(SLASH_HOTPLUG_IOCTL_MAGIC, 0x33, struct slash_hotplug_device_request)

#endif /* SLASH_HOTPLUG_UAPI_H */
