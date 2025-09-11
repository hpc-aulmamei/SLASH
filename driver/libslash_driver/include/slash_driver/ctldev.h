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

#ifndef LIBSLASH_DRIVER_CTLDEV_H
#define LIBSLASH_DRIVER_CTLDEV_H

#include "errors.h"
#include "uapi/slash_driver_interface.h"

#include <stddef.h>

#include <linux/dma-buf.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct slash_ctldev {
    int fd;
};

struct slash_bar_file {
    void *map;
    size_t len;
    int fd;
};

struct slash_ctldev *slash_ctldev_open(const char *path, enum slash_error *error);
void slash_ctldev_close(struct slash_ctldev *ctldev, enum slash_error *error);

struct slash_ioctl_bar_info *slash_bar_info_read(struct slash_ctldev *ctldev, int bar_number, enum slash_error *error);
void slash_bar_info_free(struct slash_ioctl_bar_info *ctldev, enum slash_error *error);

struct slash_bar_file *slash_bar_file_open(struct slash_ctldev *ctldev, int bar_number, int flags, enum slash_error *error);
void slash_bar_file_close(struct slash_bar_file *bar_file, enum slash_error *error);

static __inline__ int slash_bar_file_sync(struct slash_bar_file *bar_file, unsigned int flags)
{
    struct dma_buf_sync sync = { .flags = flags };

    return ioctl(bar_file->fd, DMA_BUF_IOCTL_SYNC, &sync);
}

static __inline__ int slash_bar_file_start_write(struct slash_bar_file *bar_file)
{
    return slash_bar_file_sync(bar_file, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
}

static __inline__ int slash_bar_file_end_write(struct slash_bar_file *bar_file)
{
    return slash_bar_file_sync(bar_file, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
}

static __inline__ int slash_bar_file_start_read(struct slash_bar_file *bar_file)
{
    return slash_bar_file_sync(bar_file, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
}

static __inline__ int slash_bar_file_end_read(struct slash_bar_file *bar_file)
{
    return slash_bar_file_sync(bar_file, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LIBSLASH_DRIVER_CTLDEV_H */
