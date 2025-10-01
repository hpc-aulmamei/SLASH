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

#ifndef SLASH_USER_H
#define SLASH_USER_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

/**
 * ioctl numbers are defined here:
 * https://www.kernel.org/doc/Documentation/userspace-api/ioctl/ioctl-number.rst
 *
 * The following codes are currently free:
 * Code: 'v'
 * Seq#: 30-BF
 *
 * We will aim to be good citizens and use a small range like 'v' 30-4F, which is 32 ioctls.
 * However, both these codes and the range used is subject to change for future versoins of the driver.
 */

struct slash_ioctl_bar_info {
    __u32 size;

    /* Userspace to kernel */
    __u8 bar_number;

    /* Kernel to userspace */
    __u8 usable;
    __u8 in_use;
    __u8 pad0;

    __u64 start_address;
    __u64 length;
};

struct slash_ioctl_bar_fd_request {
    __u32 size;

    /* Userspace to kernel */
    __u8  bar_number;
    __u8  pad0;
    __u16 pad1;

    __u32 flags; /* Only O_CLOEXEC */

    /* Kernel to userspace */
    __u64 length;
};

#define SLASH_CTLDEV_IOCTL_GET_BAR_INFO _IOWR('v', 0x30, struct slash_ioctl_bar_info)
#define SLASH_CTLDEV_IOCTL_GET_BAR_FD   _IOWR('v', 0x31, struct slash_ioctl_bar_fd_request)

#endif
