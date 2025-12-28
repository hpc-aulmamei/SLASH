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

#ifndef LIBSLASH_CTLDEV_MOCK_H
#define LIBSLASH_CTLDEV_MOCK_H

#include <slash/ctldev.h>

struct slash_ctldev *slash_ctldev_mock_open(void);
int slash_ctldev_mock_close(struct slash_ctldev *ctldev);

struct slash_ioctl_bar_info *slash_bar_info_mock_read(struct slash_ctldev *ctldev, int bar_number);
void slash_bar_info_mock_free(struct slash_ioctl_bar_info *ctldev);

struct slash_bar_file *slash_bar_file_mock_open(struct slash_ctldev *ctldev, int bar_number, int flags);
int slash_bar_file_mock_close(struct slash_bar_file *bar_file);

#endif /* LIBSLASH_CTLDEV_MOCK_H */
