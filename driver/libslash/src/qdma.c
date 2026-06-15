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
 * @file qdma.c
 *
 * Implementation of the slash QDMA userspace wrapper.
 *
 * Each public function validates its arguments, then issues a single
 * ioctl against the QDMA character device. No mock path exists yet.
 *
 * The ioctl structs use a size field for kernel-side version
 * negotiation: userspace sets size = sizeof(struct), and the kernel
 * can handle older/newer struct layouts accordingly.
 */

#define _GNU_SOURCE

#include <slash/qdma.h>

#include "qdma_mock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/ioctl.h>

#define QPAIR_FALLBACK_MAX_BUFS 128

struct qpair_fallback_buf {
    int      in_use;
    void    *addr;
    uint64_t length;
};

/*
 * Small process-local fallback table used only when qpair-fd registration
 * ioctls return ENOTTY (the memfd-backed @mock path).  Real hardware qpair fds
 * implement the ioctl in the kernel and never use this table.
 */
static struct qpair_fallback_buf qpair_fallback_bufs[QPAIR_FALLBACK_MAX_BUFS];

static int qpair_fallback_register(void *addr, uint64_t length, uint32_t *buf_id,
                                   enum slash_qdma_transfer_hint *transfer_hint)
{
    uint32_t i;

    if (addr == NULL || length == 0 || buf_id == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < QPAIR_FALLBACK_MAX_BUFS; ++i) {
        if (!qpair_fallback_bufs[i].in_use) {
            qpair_fallback_bufs[i].in_use = 1;
            qpair_fallback_bufs[i].addr = addr;
            qpair_fallback_bufs[i].length = length;
            *buf_id = i;
            if (transfer_hint != NULL) {
                *transfer_hint = SLASH_QDMA_TRANSFER_HINT_V80;
            }
            return 0;
        }
    }

    errno = ENOSPC;
    return -1;
}

static int qpair_fallback_unregister(uint32_t buf_id)
{
    if (buf_id >= QPAIR_FALLBACK_MAX_BUFS || !qpair_fallback_bufs[buf_id].in_use) {
        errno = ENOENT;
        return -1;
    }

    memset(&qpair_fallback_bufs[buf_id], 0, sizeof(qpair_fallback_bufs[buf_id]));
    return 0;
}

static ssize_t qpair_fallback_transfer(int qpair_fd, uint32_t buf_id,
                                       uint64_t buf_offset, uint64_t dev_addr,
                                       uint64_t length, uint32_t direction)
{
    struct qpair_fallback_buf *buf;
    char *host;
    uint64_t done = 0;

    if (qpair_fd < 0 || buf_id >= QPAIR_FALLBACK_MAX_BUFS ||
        !qpair_fallback_bufs[buf_id].in_use) {
        errno = EINVAL;
        return -1;
    }

    buf = &qpair_fallback_bufs[buf_id];
    if (length == 0 || buf_offset > buf->length || length > buf->length - buf_offset) {
        errno = EINVAL;
        return -1;
    }

    host = (char *)buf->addr + buf_offset;
    while (done < length) {
        ssize_t n;

        if (direction == SLASH_QDMA_XFER_H2C) {
            n = pwrite(qpair_fd, host + done, (size_t)(length - done),
                       (off_t)(dev_addr + done));
        } else if (direction == SLASH_QDMA_XFER_C2H) {
            n = pread(qpair_fd, host + done, (size_t)(length - done),
                      (off_t)(dev_addr + done));
        } else {
            errno = EINVAL;
            return -1;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        done += (uint64_t)n;
    }

    return (ssize_t)done;
}

struct slash_qdma *slash_qdma_open(const char *path)
{
    struct slash_qdma *qdma;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (strcmp(path, "@mock") == 0) {
        return slash_qdma_mock_open();
    }

    qdma = calloc(1, sizeof(*qdma));
    if (qdma == NULL) {
        return NULL;
    }

    qdma->fd = open(path, O_RDWR);
    if (qdma->fd < 0) {
        free(qdma);
        return NULL;
    }

    return qdma;
}

int slash_qdma_close(struct slash_qdma *qdma)
{
    int ret;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_close(qdma);
    }

    ret = 0;
    if (qdma->fd >= 0 && close(qdma->fd) != 0) {
        ret = -1;
    }

    /* Free unconditionally — handle is invalid after this call. */
    free(qdma);

    return ret;
}

int slash_qdma_info_read(struct slash_qdma *qdma, struct slash_qdma_info *info)
{
    struct slash_qdma_info tmp;
    int ret;

    if (qdma == NULL || info == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_info_read(qdma, info);
    }

    memset(&tmp, 0, sizeof(tmp));
    tmp.size = sizeof(tmp);

    ret = ioctl(qdma->fd, SLASH_QDMA_IOCTL_INFO, &tmp);
    if (ret < 0) {
        return -1;
    }

    /* Copy the kernel-filled result back to the caller. */
    *info = tmp;

    return 0;
}

/**
 * slash_qdma_qpair_add() — Create a new queue pair.
 *
 * Copies caller-provided configuration into a zeroed temporary to
 * ensure no stale fields leak to the kernel, then copies the full
 * kernel response (including assigned qid) back into @req.
 */
int slash_qdma_qpair_add(struct slash_qdma *qdma,
                         struct slash_qdma_qpair_add *req)
{
    struct slash_qdma_qpair_add tmp;
    int ret;

    if (qdma == NULL || req == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_qpair_add(qdma, req);
    }

    memset(&tmp, 0, sizeof(tmp));
    tmp.size        = sizeof(tmp);
    tmp.mode        = req->mode;
    tmp.dir_mask    = req->dir_mask;
    tmp.mm_channel  = req->mm_channel;
    tmp.h2c_ring_sz = req->h2c_ring_sz;
    tmp.c2h_ring_sz = req->c2h_ring_sz;
    tmp.cmpt_ring_sz = req->cmpt_ring_sz;

    ret = ioctl(qdma->fd, SLASH_QDMA_IOCTL_QPAIR_ADD, &tmp);
    if (ret < 0) {
        return -1;
    }

    /* Write back — kernel will have filled in qid and other fields. */
    *req = tmp;

    return 0;
}

/**
 * slash_qdma_qpair_op() — Issue a queue pair lifecycle operation.
 *
 * Internal helper shared by start/stop/del. The @op parameter selects
 * which operation the kernel performs.
 */
static int slash_qdma_qpair_op(struct slash_qdma *qdma,
                               uint32_t qid,
                               uint32_t op)
{
    struct slash_qdma_qpair_op req;
    int ret;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        switch (op) {
        case SLASH_QDMA_QUEUE_OP_START:
            return slash_qdma_mock_qpair_start(qdma, qid);
        case SLASH_QDMA_QUEUE_OP_STOP:
            return slash_qdma_mock_qpair_stop(qdma, qid);
        case SLASH_QDMA_QUEUE_OP_DEL:
            return slash_qdma_mock_qpair_del(qdma, qid);
        default:
            errno = EINVAL;
            return -1;
        }
    }

    memset(&req, 0, sizeof(req));
    req.size = sizeof(req);
    req.qid  = qid;
    req.op   = op;

    ret = ioctl(qdma->fd, SLASH_QDMA_IOCTL_Q_OP, &req);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

int slash_qdma_qpair_start(struct slash_qdma *qdma, uint32_t qid)
{
    return slash_qdma_qpair_op(qdma, qid, SLASH_QDMA_QUEUE_OP_START);
}

int slash_qdma_qpair_stop(struct slash_qdma *qdma, uint32_t qid)
{
    return slash_qdma_qpair_op(qdma, qid, SLASH_QDMA_QUEUE_OP_STOP);
}

int slash_qdma_qpair_del(struct slash_qdma *qdma, uint32_t qid)
{
    return slash_qdma_qpair_op(qdma, qid, SLASH_QDMA_QUEUE_OP_DEL);
}

int slash_qdma_qpair_get_fd(struct slash_qdma *qdma, uint32_t qid, int flags)
{
    struct slash_qdma_qpair_fd_request req;
    int fd;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_qpair_get_fd(qdma, qid, flags);
    }

    memset(&req, 0, sizeof(req));
    req.size  = sizeof(req);
    req.qid   = qid;
    req.flags = flags;

    fd = ioctl(qdma->fd, SLASH_QDMA_IOCTL_QPAIR_GET_FD, &req);
    if (fd < 0) {
        return -1;
    }

    return fd;
}

int slash_qdma_buffer_register(struct slash_qdma *qdma, void *addr,
                               uint64_t length, uint32_t *buf_id,
                               enum slash_qdma_transfer_hint *transfer_hint)
{
    struct slash_qdma_buf_register req;
    int ret;

    if (qdma == NULL || addr == NULL || buf_id == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_buffer_register(qdma, addr, length, buf_id,
                                               transfer_hint);
    }

    memset(&req, 0, sizeof(req));
    req.size      = sizeof(req);
    req.user_addr = (uint64_t)(uintptr_t)addr;
    req.length    = length;

    ret = ioctl(qdma->fd, SLASH_QDMA_IOCTL_BUF_REGISTER, &req);
    if (ret < 0) {
        return -1;
    }

    *buf_id = req.buf_id;
    if (transfer_hint != NULL) {
        *transfer_hint = req.transfer_hint;
    }

    return 0;
}

int slash_qdma_buffer_unregister(struct slash_qdma *qdma, uint32_t buf_id)
{
    struct slash_qdma_buf_unregister req;
    int ret;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_buffer_unregister(qdma, buf_id);
    }

    memset(&req, 0, sizeof(req));
    req.size   = sizeof(req);
    req.buf_id = buf_id;

    ret = ioctl(qdma->fd, SLASH_QDMA_IOCTL_BUF_UNREGISTER, &req);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

int slash_qdma_qpair_buffer_register(int qpair_fd, void *addr,
                                     uint64_t length, uint32_t *buf_id,
                                     enum slash_qdma_transfer_hint *transfer_hint)
{
    struct slash_qdma_buf_register req;
    int ret;

    if (qpair_fd < 0 || addr == NULL || buf_id == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.size      = sizeof(req);
    req.user_addr = (uint64_t)(uintptr_t)addr;
    req.length    = length;

    ret = ioctl(qpair_fd, SLASH_QDMA_IOCTL_BUF_REGISTER, &req);
    if (ret < 0) {
        if (errno == ENOTTY) {
            return qpair_fallback_register(addr, length, buf_id, transfer_hint);
        }
        return -1;
    }

    *buf_id = req.buf_id;
    if (transfer_hint != NULL) {
        *transfer_hint = req.transfer_hint;
    }

    return 0;
}

int slash_qdma_qpair_buffer_unregister(int qpair_fd, uint32_t buf_id)
{
    struct slash_qdma_buf_unregister req;
    int ret;

    if (qpair_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.size   = sizeof(req);
    req.buf_id = buf_id;

    ret = ioctl(qpair_fd, SLASH_QDMA_IOCTL_BUF_UNREGISTER, &req);
    if (ret < 0) {
        if (errno == ENOTTY) {
            return qpair_fallback_unregister(buf_id);
        }
        return -1;
    }

    return 0;
}

ssize_t slash_qdma_transfer(struct slash_qdma *qdma, int qpair_fd,
                            uint32_t buf_id, uint64_t buf_offset,
                            uint64_t dev_addr, uint64_t length,
                            uint32_t direction)
{
    struct slash_qdma_transfer req;
    int ret;

    if (qdma == NULL || qpair_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (direction != SLASH_QDMA_XFER_H2C && direction != SLASH_QDMA_XFER_C2H) {
        errno = EINVAL;
        return -1;
    }

    if (qdma->priv) {
        return slash_qdma_mock_transfer(qdma, qpair_fd, buf_id, buf_offset,
                                        dev_addr, length, direction);
    }

    return slash_qdma_qpair_transfer(qpair_fd, buf_id, buf_offset, dev_addr,
                                     length, direction);
}

ssize_t slash_qdma_qpair_transfer(int qpair_fd, uint32_t buf_id,
                                  uint64_t buf_offset, uint64_t dev_addr,
                                  uint64_t length, uint32_t direction)
{
    struct slash_qdma_transfer req;
    int ret;

    if (qpair_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (direction != SLASH_QDMA_XFER_H2C && direction != SLASH_QDMA_XFER_C2H) {
        errno = EINVAL;
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.size       = sizeof(req);
    req.buf_id     = buf_id;
    req.buf_offset = buf_offset;
    req.dev_addr   = dev_addr;
    req.length     = length;
    req.direction  = direction;

    ret = ioctl(qpair_fd, SLASH_QDMA_QPAIR_IOCTL_TRANSFER, &req);
    if (ret < 0) {
        if (errno == ENOTTY) {
            return qpair_fallback_transfer(qpair_fd, buf_id, buf_offset,
                                           dev_addr, length, direction);
        }
        return -1;
    }

    return (ssize_t)ret;
}

