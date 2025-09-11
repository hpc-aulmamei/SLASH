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

#define _GNU_SOURCE

#include <slash_driver/ctldev.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <errno.h>

#include <time.h>
#include <stdint.h>
#include <string.h>

#include <time.h>

#include <stdio.h>
#include <sys/mman.h>

#define SLASH_ERROR_SETUP(error) \
    enum slash_error _slash_fallback_error; \
    error = (error) ? error : &_slash_fallback_error; \
    *error = SLASH_ERROR_OK

struct slash_ctldev *slash_ctldev_open(const char *path, enum slash_error *error)
{
    struct slash_ctldev *ctldev;

    SLASH_ERROR_SETUP(error);

    ctldev = calloc(1, sizeof(*ctldev));
    if (ctldev == NULL) {
        *error = SLASH_ERROR_SYS;
        return NULL;
    }

    ctldev->fd = open(path, O_RDWR);
    if (ctldev->fd < 0) {
        *error = SLASH_ERROR_SYS;
        goto err_free_ctldev;
    }

    return ctldev;

err_free_ctldev:
    free(ctldev);

    return NULL;
}

void slash_ctldev_close(struct slash_ctldev *ctldev, enum slash_error *error)
{
    int ret;

    SLASH_ERROR_SETUP(error);
 
    if (ctldev == NULL) {
        return;
    }

    ret = close(ctldev->fd);
    if (ret < 0) {
        *error = SLASH_ERROR_SYS;
    }

    free(ctldev);
}

struct slash_ioctl_bar_info *slash_bar_info_read(struct slash_ctldev *ctldev, int bar_number, enum slash_error *error)
{
    int ret;
    struct slash_ioctl_bar_info *bar_info;

    SLASH_ERROR_SETUP(error);

    bar_info = calloc(1, sizeof(*bar_info));
    if (bar_info == NULL) {
        *error = SLASH_ERROR_SYS;
        return NULL;
    }

    bar_info->size = sizeof(*bar_info);
    bar_info->bar_number = bar_number;

    ret = ioctl(ctldev->fd, SLASH_CTLDEV_IOCTL_GET_BAR_INFO, bar_info);
    if (ret < 0) {
        *error = SLASH_ERROR_SYS;
        goto err_free_bar_info;
    }

    return bar_info;

err_free_bar_info:
    free(bar_info);

    return NULL;
}

struct slash_bar_file *slash_bar_file_open(struct slash_ctldev *ctldev, int bar_number, int flags, enum slash_error *error)
{
    struct slash_ioctl_bar_fd_request req = {
        .size = sizeof(req),

        .bar_number = bar_number,
        .flags = flags,
    };

    struct slash_bar_file *bar_file;

    SLASH_ERROR_SETUP(error);
    
    bar_file = calloc(1, sizeof(*bar_file));
    if (bar_file == NULL) {
        *error = SLASH_ERROR_SYS;
        return NULL;
    }

    bar_file->fd = ioctl(ctldev->fd, SLASH_CTLDEV_IOCTL_GET_BAR_FD, &req);
    if (bar_file->fd < 0) {
        *error = SLASH_ERROR_SYS;
        
        goto err_free_bar_file;
    }

    bar_file->len = (size_t) req.length;

    bar_file->map = mmap(NULL, bar_file->len, PROT_READ | PROT_WRITE, MAP_SHARED, bar_file->fd, 0);
    if (bar_file->map == MAP_FAILED) {
        *error = SLASH_ERROR_SYS;
        goto err_close_fd;
    }

    return bar_file;

err_close_fd:
    (void) close(bar_file->fd);

err_free_bar_file:
    free(bar_file);

    return NULL;
}

void slash_bar_file_close(struct slash_bar_file *bar_file, enum slash_error *error)
{
    int ret;

    SLASH_ERROR_SETUP(error);

    ret = munmap(bar_file->map, bar_file->len);
    if (ret < 0) {
        *error = SLASH_ERROR_SYS;
    }

    ret = close(bar_file->fd);
    if (ret < 0) {
        *error = SLASH_ERROR_SYS;
    }

    free(bar_file);
}

void slash_bar_info_free(struct slash_ioctl_bar_info *bar_info, enum slash_error *error)
{
    (void) error;

    free(bar_info);
}
