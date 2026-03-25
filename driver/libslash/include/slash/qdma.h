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

/**
 * @file qdma.h
 *
 * Userspace API for slash QDMA (Queue-based DMA) devices.
 *
 * A QDMA device is a separate misc character device created for PF1,
 * while the control device (ctldev) is created for PF2.  Each PCI
 * function gets at most one of each.  Device nodes appear at
 * /dev/slash_qdma_ctl0, /dev/slash_qdma_ctl1, etc.
 *
 * Queue pair lifecycle:
 *   1. slash_qdma_open()         — open the QDMA device
 *   2. slash_qdma_qpair_add()   — create a queue pair (returns assigned qid)
 *   3. slash_qdma_qpair_start() — activate for transfers
 *   4. slash_qdma_qpair_get_fd() — obtain fd for data transfer
 *   5. slash_qdma_qpair_stop()  — deactivate
 *   6. slash_qdma_qpair_del()   — destroy
 *   7. slash_qdma_close()       — close the device
 *
 * The fd from qpair_get_fd() supports read() for C2H (card-to-host)
 * and write() for H2C (host-to-card) DMA transfers.  Positional I/O
 * via lseek()/pread()/pwrite() is also supported.  splice(), mmap(),
 * and poll() are not available.
 *
 * Error conventions: int-returning functions return -1 with errno set.
 * Pointer-returning functions return NULL with errno set.
 */

#ifndef LIBSLASH_QDMA_H
#define LIBSLASH_QDMA_H

#include "uapi/slash_interface.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * struct slash_qdma — Handle to an open QDMA device.
 * @fd:   File descriptor for the QDMA character device.
 * @mock: Reserved for mock support.
 *
 * @mock is reserved for future use and is always set to false.
 */
struct slash_qdma {
    int fd;
    bool mock;
};

/**
 * slash_qdma_open() — Open a QDMA device.
 * @path: Path to the character device node. NULL returns NULL/EINVAL.
 *
 * Return: Heap-allocated handle on success, NULL on failure.
 */
struct slash_qdma *slash_qdma_open(const char *path);

/**
 * slash_qdma_close() — Close a QDMA device and free the handle.
 * @qdma: Handle from slash_qdma_open(), or NULL (returns -1/EINVAL).
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_close(struct slash_qdma *qdma);

/**
 * slash_qdma_info_read() — Read QDMA device capabilities.
 * @qdma: Open QDMA handle.
 * @info: Caller-allocated struct, filled in on success.
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_info_read(struct slash_qdma *qdma, struct slash_qdma_info *info);

/**
 * slash_qdma_qpair_add() — Create a new queue pair.
 * @qdma: Open QDMA handle.
 * @req:  In/out — caller sets configuration fields, kernel fills in
 *        the assigned queue id (and possibly other output fields).
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_qpair_add(struct slash_qdma *qdma,
                         struct slash_qdma_qpair_add *req);

/**
 * slash_qdma_qpair_start() — Activate a queue pair for transfers.
 * @qdma: Open QDMA handle.
 * @qid:  Queue pair id from slash_qdma_qpair_add().
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_qpair_start(struct slash_qdma *qdma, uint32_t qid);

/**
 * slash_qdma_qpair_stop() — Deactivate a queue pair.
 * @qdma: Open QDMA handle.
 * @qid:  Queue pair id.
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_qpair_stop(struct slash_qdma *qdma, uint32_t qid);

/**
 * slash_qdma_qpair_del() — Destroy a queue pair.
 * @qdma: Open QDMA handle.
 * @qid:  Queue pair id.
 *
 * The kernel implicitly stops the queue if it is still running, so a
 * separate stop call is not required before del.
 *
 * Return: 0 on success, -1 on failure.
 */
int slash_qdma_qpair_del(struct slash_qdma *qdma, uint32_t qid);

/**
 * slash_qdma_qpair_get_fd() — Obtain a file descriptor for data transfer.
 * @qdma:  Open QDMA handle.
 * @qid:   Queue pair id (must be started).
 * @flags: Only O_CLOEXEC is accepted; the kernel returns -EINVAL for
 *         any other bits.
 *
 * The returned fd supports read() (C2H) and write() (H2C).  Positional
 * I/O via lseek()/pread()/pwrite() is also available.
 *
 * Return: Non-negative fd on success, -1 on failure.
 */
int slash_qdma_qpair_get_fd(struct slash_qdma *qdma, uint32_t qid, int flags);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LIBSLASH_QDMA_H */

