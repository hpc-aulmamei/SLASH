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

#include <slash/ctldev.h>

#include "ctldev_mock.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <errno.h>

#include <stdint.h>
#include <string.h>

#include <stdio.h>
#include <sys/mman.h>

struct slash_ctldev *slash_ctldev_open(const char *path)
{
    struct slash_ctldev *ctldev;

    if (path != NULL && strcmp(path, "@mock") == 0) {
        return slash_ctldev_mock_open();
    }

    ctldev = calloc(1, sizeof(*ctldev));
    if (ctldev == NULL) {
        return NULL;
    }

    ctldev->fd = open(path, O_RDWR);
    if (ctldev->fd < 0) {
        goto err_free_ctldev;
    }

    ctldev->mock = false;

    return ctldev;

err_free_ctldev:
    free(ctldev);

    return NULL;
}

int slash_ctldev_close(struct slash_ctldev *ctldev)
{
    int ret;

    if (ctldev == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ctldev->mock) {
        return slash_ctldev_mock_close(ctldev);
    }

    ret = 0;
    if (ctldev->fd >= 0 && close(ctldev->fd) != 0) {
        ret = -1;
    }

    free(ctldev);

    return ret;
}

struct slash_ioctl_bar_info *slash_bar_info_read(struct slash_ctldev *ctldev, int bar_number)
{
    int ret;
    struct slash_ioctl_bar_info *bar_info;

    if (ctldev == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (ctldev->mock) {
        return slash_bar_info_mock_read(ctldev, bar_number);
    }

    bar_info = calloc(1, sizeof(*bar_info));
    if (bar_info == NULL) {
        return NULL;
    }

    bar_info->size = sizeof(*bar_info);
    bar_info->bar_number = bar_number;

    ret = ioctl(ctldev->fd, SLASH_CTLDEV_IOCTL_GET_BAR_INFO, bar_info);
    if (ret < 0) {
        goto err_free_bar_info;
    }

    return bar_info;

err_free_bar_info:
    free(bar_info);

    return NULL;
}

struct slash_bar_file *slash_bar_file_open(struct slash_ctldev *ctldev, int bar_number, int flags)
{
    struct slash_ioctl_bar_fd_request req = {
        .size = sizeof(req),

        .bar_number = bar_number,
        .flags = flags,
    };

    struct slash_bar_file *bar_file;

    if (ctldev == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (ctldev->mock) {
        return slash_bar_file_mock_open(ctldev, bar_number, flags);
    }

    bar_file = calloc(1, sizeof(*bar_file));
    if (bar_file == NULL) {
        return NULL;
    }

    bar_file->fd = ioctl(ctldev->fd, SLASH_CTLDEV_IOCTL_GET_BAR_FD, &req);
    if (bar_file->fd < 0) {
        goto err_free_bar_file;
    }

    bar_file->len = (size_t) req.length;

    bar_file->map = mmap(NULL, bar_file->len, PROT_READ | PROT_WRITE, MAP_SHARED, bar_file->fd, 0);
    if (bar_file->map == MAP_FAILED) {
        goto err_close_fd;
    }

    bar_file->mock = false;

    return bar_file;

err_close_fd:
    (void) close(bar_file->fd);

err_free_bar_file:
    free(bar_file);

    return NULL;
}

int slash_bar_file_close(struct slash_bar_file *bar_file)
{
    int ret = 0;

    if (bar_file == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (bar_file->mock) {
        return slash_bar_file_mock_close(bar_file);
    }

    if (munmap(bar_file->map, bar_file->len) != 0) {
        ret = -1;
    }

    if (close(bar_file->fd) != 0) {
        ret = -1;
    }

    free(bar_file);

    return ret;
}

void slash_bar_info_free(struct slash_ioctl_bar_info *bar_info)
{
    free(bar_info);
}
