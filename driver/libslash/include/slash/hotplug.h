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

#ifndef LIBSLASH_HOTPLUG_H
#define LIBSLASH_HOTPLUG_H

#include "uapi/slash_hotplug.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SLASH_HOTPLUG_DEFAULT_PATH "/dev/" SLASH_HOTPLUG_DEVICE_NAME

struct slash_hotplug {
    int fd;
};

struct slash_hotplug *slash_hotplug_open(const char *path);
int slash_hotplug_close(struct slash_hotplug *hotplug);

int slash_hotplug_rescan(struct slash_hotplug *hotplug);
int slash_hotplug_remove(struct slash_hotplug *hotplug, const char *bdf);
int slash_hotplug_toggle_sbr(struct slash_hotplug *hotplug, const char *bdf);
int slash_hotplug_hotplug(struct slash_hotplug *hotplug, const char *bdf);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LIBSLASH_HOTPLUG_H */
