/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "buffer.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include <systemd/sd-journal.h>

#define VRTD_QDMA_Q_MODE_MM 0u
#define VRTD_QDMA_DIR_H2C (1u << 0)
#define VRTD_QDMA_DIR_C2H (1u << 1)
#define VRTD_QDMA_RING_SZ_IDX 0u

static int buffer_init(struct buffer *buf,
                       struct slash_qdma *qdma,
                       struct device_memory_map *map,
                       enum allocation_type alloc_type,
                       enum vrtd_alloc_dir alloc_dir,
                       uint64_t size,
                       uint64_t alloc_arg,
                       uint64_t client_id,
                       const struct slash_qdma_qpair_add *qpair_params)
{
    if (buf == NULL) {
        errno = EINVAL;
        LOG(LOG_ERR, "Failed to initialize buffer: invalid output pointer");
        return -1;
    }

    *buf = (struct buffer) {
        .qdma = qdma,
        .map = map,
        .alloc_type = alloc_type,
        .alloc_arg = alloc_arg,
        .alloc_dir = alloc_dir,
        .client_id = client_id,
        .addr = 0,
        .size = 0,
        .qid = 0,
        .fd = -1,
        .allocation_valid = false,
        .qpair_created = false,
    };

    if (qdma == NULL || map == NULL || size == 0 || client_id == 0) {
        errno = EINVAL;
        LOG(
            LOG_ERR,
            "Failed to initialize buffer: invalid arguments (qdma=%p map=%p size=%llu client_id=%llu)",
            (void *)qdma,
            (void *)map,
            (unsigned long long)size,
            (unsigned long long)client_id
        );
        goto fail;
    }

    uint32_t dir_mask = 0;
    switch (alloc_dir) {
    case VRTD_ALLOC_DIR_BIDIRECTIONAL:
        dir_mask = VRTD_QDMA_DIR_H2C | VRTD_QDMA_DIR_C2H;
        break;
    case VRTD_ALLOC_DIR_HOST_TO_DEVICE:
        dir_mask = VRTD_QDMA_DIR_H2C;
        break;
    case VRTD_ALLOC_DIR_DEVICE_TO_HOST:
        dir_mask = VRTD_QDMA_DIR_C2H;
        break;
    default:
        errno = EINVAL;
        LOG(
            LOG_ERR,
            "Failed to initialize buffer: invalid allocation direction %u",
            (unsigned int)alloc_dir
        );
        goto fail;
    }

    uint64_t alloc_size = size;
    uint64_t alloc_addr = 0;
    enum allocation_result ares = device_memory_map_allocate(
        map,
        alloc_type,
        &alloc_size,
        alloc_arg,
        client_id,
        &alloc_addr
    );
    if (ares != ALLOCATION_RESULT_SUCCESS) {
        errno = (ares == ALLOCATION_RESULT_NO_MEMORY) ? ENOMEM : EINVAL;
        LOG(
            LOG_ERR,
            "Failed to allocate device memory for buffer (result=%d alloc_type=%u size=%llu alloc_arg=%llu client_id=%llu): %m",
            (int)ares,
            (unsigned int)alloc_type,
            (unsigned long long)size,
            (unsigned long long)alloc_arg,
            (unsigned long long)client_id
        );
        goto fail;
    }

    buf->addr = alloc_addr;
    buf->size = alloc_size;
    buf->allocation_valid = true;

    struct slash_qdma_qpair_add qpair = {0};
    if (qpair_params != NULL) {
        qpair = *qpair_params;
    } else {
        qpair.mode = VRTD_QDMA_Q_MODE_MM;
        qpair.h2c_ring_sz = VRTD_QDMA_RING_SZ_IDX;
        qpair.c2h_ring_sz = VRTD_QDMA_RING_SZ_IDX;
        qpair.cmpt_ring_sz = VRTD_QDMA_RING_SZ_IDX;
    }
    qpair.dir_mask = dir_mask;
    qpair.size = sizeof(qpair);

    if (slash_qdma_qpair_add(qdma, &qpair) != 0) {
        LOG(LOG_ERR, "Failed to add buffer qpair: %m");
        goto fail;
    }

    buf->qid = qpair.qid;
    buf->qpair_created = true;

    if (slash_qdma_qpair_start(qdma, buf->qid) != 0) {
        LOG(LOG_ERR, "Failed to start buffer qpair %u: %m", buf->qid);
        goto fail;
    }

    int fd = slash_qdma_qpair_get_fd(qdma, buf->qid, O_CLOEXEC);
    if (fd < 0) {
        LOG(LOG_ERR, "Failed to get fd for buffer qpair %u: %m", buf->qid);
        goto fail;
    }
    buf->fd = fd;

    LOG(LOG_DEBUG, "Buffer initialized addr=0x%llx size=%llu qid=%u", (unsigned long long)buf->addr, (unsigned long long)buf->size, buf->qid);
    return 0;

fail:
    cleanup_buffer(buf);
    return -1;
}

struct buffer *buffer_create(struct slash_qdma *qdma,
                             struct device_memory_map *map,
                             enum allocation_type alloc_type,
                             enum vrtd_alloc_dir alloc_dir,
                             uint64_t size,
                             uint64_t alloc_arg,
                             uint64_t client_id,
                             const struct slash_qdma_qpair_add *qpair_params)
{
    struct buffer *buf = calloc(1, sizeof(*buf));
    if (buf == NULL) {
        LOG(LOG_ERR, "Failed to allocate buffer: %m");
        return NULL;
    }

    if (buffer_init(buf, qdma, map, alloc_type, alloc_dir, size, alloc_arg, client_id, qpair_params) != 0) {
        LOG(LOG_ERR, "Failed to initialize buffer: %m");
        return NULL;
    }

    return buf;
}

void cleanup_buffer(struct buffer *buf)
{
    if (buf == NULL) {
        return;
    }

    LOG(LOG_DEBUG, "Freeing buffer addr=0x%llx size=%llu qid=%u", (unsigned long long)buf->addr, (unsigned long long)buf->size, buf->qid);

    if (buf->fd >= 0) {
        (void) close(buf->fd);
        buf->fd = -1;
    }

    if (buf->qpair_created && buf->qdma != NULL) {
        if (slash_qdma_qpair_stop(buf->qdma, buf->qid) != 0) {
            LOG(
                LOG_WARNING,
                "Error stopping buffer qpair %u: %m (ignored)",
                buf->qid
            );
        }
        if (slash_qdma_qpair_del(buf->qdma, buf->qid) != 0) {
            LOG(
                LOG_WARNING,
                "Error deleting buffer qpair %u: %m (ignored)",
                buf->qid
            );
        }
    }

    if (buf->allocation_valid && buf->map != NULL) {
        if (device_memory_map_free(
                buf->map,
                buf->alloc_type,
                buf->addr,
                buf->size,
                buf->client_id
            ) != ALLOCATION_RESULT_SUCCESS) {
            LOG(
                LOG_WARNING,
                "Error freeing buffer allocation (addr=0x%llx size=%llu): %m (ignored)",
                (unsigned long long)buf->addr,
                (unsigned long long)buf->size
            );
        }
    }

    buf->qdma = NULL;
    buf->map = NULL;
    buf->qpair_created = false;
    buf->allocation_valid = false;
    buf->addr = 0;
    buf->size = 0;
    buf->qid = 0;
    buf->fd = -1;

    free(buf);
}
