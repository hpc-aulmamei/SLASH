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
 * @file qdma_mock.c
 * @brief Mock QDMA implementation backed by memfd files.
 *
 * Each queue pair's I/O fd is a memfd_create() anonymous file.  The kernel
 * supports pread()/pwrite() at arbitrary offsets on memfds (tmpfs), so the
 * test's DDR_BASE_ADDRESS offset is handled transparently via sparse pages.
 *
 * Queue state is tracked in a fixed-size table (QDMA_MOCK_MAX_QUEUES slots)
 * stored in the slash_qdma_mock struct pointed to by qdma->priv.
 */

#define _GNU_SOURCE

#include "qdma_mock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#define QDMA_MOCK_MAX_QUEUES 64
#define QDMA_MOCK_MAX_BUFS 64

struct slash_qdma_mock_qpair {
    bool in_use;
    bool started;
    int  fd; /* backing memfd; -1 when slot is free */
};

struct slash_qdma_mock_buf {
    bool      in_use;
    void     *addr;   /* host base address */
    uint64_t  length;
};

struct slash_qdma_mock {
    struct slash_qdma_mock_qpair queues[QDMA_MOCK_MAX_QUEUES];
    struct slash_qdma_mock_buf   bufs[QDMA_MOCK_MAX_BUFS];
};

static struct slash_qdma_mock *mock_ctx(struct slash_qdma *qdma)
{
    return (struct slash_qdma_mock *) qdma->priv;
}

struct slash_qdma *slash_qdma_mock_open(void)
{
    struct slash_qdma *qdma;
    struct slash_qdma_mock *ctx;
    size_t i;

    qdma = calloc(1, sizeof(*qdma));
    if (qdma == NULL) {
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        free(qdma);
        return NULL;
    }

    for (i = 0; i < QDMA_MOCK_MAX_QUEUES; ++i) {
        ctx->queues[i].fd = -1;
    }

    qdma->fd   = -1;
    qdma->priv = ctx;

    return qdma;
}

int slash_qdma_mock_close(struct slash_qdma *qdma)
{
    struct slash_qdma_mock *ctx;
    size_t i;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    for (i = 0; i < QDMA_MOCK_MAX_QUEUES; ++i) {
        if (ctx->queues[i].in_use && ctx->queues[i].fd >= 0) {
            (void) close(ctx->queues[i].fd);
        }
    }

    free(ctx);
    free(qdma);

    return 0;
}

int slash_qdma_mock_info_read(struct slash_qdma *qdma, struct slash_qdma_info *info)
{
    if (qdma == NULL || info == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->size      = sizeof(*info);
    info->qsets_max = QDMA_MOCK_MAX_QUEUES;
    info->msix_qvecs = 1;

    return 0;
}

int slash_qdma_mock_qpair_add(struct slash_qdma *qdma, struct slash_qdma_qpair_add *req)
{
    struct slash_qdma_mock *ctx;
    size_t i;
    int fd;

    if (qdma == NULL || req == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    for (i = 0; i < QDMA_MOCK_MAX_QUEUES; ++i) {
        if (!ctx->queues[i].in_use) {
            break;
        }
    }

    if (i == QDMA_MOCK_MAX_QUEUES) {
        errno = ENOSPC;
        return -1;
    }

    fd = memfd_create("slash_qdma_mock", MFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    ctx->queues[i].in_use  = true;
    ctx->queues[i].started = false;
    ctx->queues[i].fd      = fd;

    req->qid = (uint32_t) i;

    return 0;
}

static int mock_qpair_op(struct slash_qdma *qdma, uint32_t qid, bool start)
{
    struct slash_qdma_mock *ctx;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qid >= QDMA_MOCK_MAX_QUEUES) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    if (!ctx->queues[qid].in_use) {
        errno = EINVAL;
        return -1;
    }

    ctx->queues[qid].started = start;

    return 0;
}

int slash_qdma_mock_qpair_start(struct slash_qdma *qdma, uint32_t qid)
{
    return mock_qpair_op(qdma, qid, true);
}

int slash_qdma_mock_qpair_stop(struct slash_qdma *qdma, uint32_t qid)
{
    return mock_qpair_op(qdma, qid, false);
}

int slash_qdma_mock_qpair_del(struct slash_qdma *qdma, uint32_t qid)
{
    struct slash_qdma_mock *ctx;

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qid >= QDMA_MOCK_MAX_QUEUES) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    if (!ctx->queues[qid].in_use) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->queues[qid].fd >= 0) {
        (void) close(ctx->queues[qid].fd);
    }

    memset(&ctx->queues[qid], 0, sizeof(ctx->queues[qid]));
    ctx->queues[qid].fd = -1;

    return 0;
}

int slash_qdma_mock_qpair_get_fd(struct slash_qdma *qdma, uint32_t qid, int flags)
{
    struct slash_qdma_mock *ctx;
    int new_fd;
    (void) flags; /* O_CLOEXEC already set on the memfd */

    if (qdma == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (qid >= QDMA_MOCK_MAX_QUEUES) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    if (!ctx->queues[qid].in_use || !ctx->queues[qid].started) {
        errno = EINVAL;
        return -1;
    }

    /* dup so the caller owns a separate fd they can close independently */
    new_fd = dup(ctx->queues[qid].fd);
    if (new_fd < 0) {
        return -1;
    }

    return new_fd;
}

int slash_qdma_mock_buffer_register(struct slash_qdma *qdma, void *addr,
                                    uint64_t length, uint32_t *buf_id,
                                    enum slash_qdma_transfer_hint *transfer_hint)
{
    struct slash_qdma_mock *ctx;
    size_t i;

    if (qdma == NULL || addr == NULL || buf_id == NULL || length == 0) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    for (i = 0; i < QDMA_MOCK_MAX_BUFS; ++i) {
        if (!ctx->bufs[i].in_use) {
            break;
        }
    }

    if (i == QDMA_MOCK_MAX_BUFS) {
        errno = ENOSPC;
        return -1;
    }

    ctx->bufs[i].in_use = true;
    ctx->bufs[i].addr   = addr;
    ctx->bufs[i].length = length;

    *buf_id = (uint32_t) i;
    if (transfer_hint != NULL) {
        *transfer_hint = SLASH_QDMA_TRANSFER_HINT_V80;
    }

    return 0;
}

int slash_qdma_mock_buffer_unregister(struct slash_qdma *qdma, uint32_t buf_id)
{
    struct slash_qdma_mock *ctx;

    if (qdma == NULL || buf_id >= QDMA_MOCK_MAX_BUFS) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);

    if (!ctx->bufs[buf_id].in_use) {
        errno = ENOENT;
        return -1;
    }

    memset(&ctx->bufs[buf_id], 0, sizeof(ctx->bufs[buf_id]));

    return 0;
}

ssize_t slash_qdma_mock_transfer(struct slash_qdma *qdma, int qpair_fd,
                                 uint32_t buf_id, uint64_t buf_offset,
                                 uint64_t dev_addr, uint64_t length,
                                 uint32_t direction)
{
    struct slash_qdma_mock *ctx;
    struct slash_qdma_mock_buf *buf;
    char *host;
    uint64_t done = 0;

    if (qdma == NULL || qpair_fd < 0 || buf_id >= QDMA_MOCK_MAX_BUFS) {
        errno = EINVAL;
        return -1;
    }

    ctx = mock_ctx(qdma);
    buf = &ctx->bufs[buf_id];

    if (!buf->in_use) {
        errno = ENOENT;
        return -1;
    }

    if (length == 0 || buf_offset > buf->length ||
        length > buf->length - buf_offset) {
        errno = EINVAL;
        return -1;
    }

    host = (char *) buf->addr + buf_offset;

    /*
     * Emulate the device endpoint with the queue's backing memfd: H2C writes
     * host data to the memfd at dev_addr, C2H reads it back.  Loop to absorb
     * short transfers from the underlying file ops.
     */
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
        done += (uint64_t) n;
    }

    return (ssize_t) done;
}
