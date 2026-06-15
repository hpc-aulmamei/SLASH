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

/**
 * @file buffer.c
 *
 * DMA buffer lifecycle management for the vrtd C client library.
 *
 * Buffers are host-side memory regions used for DMA transfers to/from
 * the FPGA.  Each buffer is backed by an anonymous mmap whose page granule
 * (4 KiB base pages or 2 MiB hugepages) is selected explicitly by the caller
 * via enum vrtd_host_page_mode -- there is no automatic fallback -- and
 * associated with a QDMA queue pair fd for performing the actual H2C / C2H
 * transfers.
 *
 * Sync operations (sync_to_device / sync_from_device) accept arbitrary
 * in-buffer ranges. Internally, the QDMA fd requires page-aligned transfer
 * ranges, so libvrtd expands partial requests to the mapping granule and uses
 * a staging buffer when needed to preserve host-side partial-range semantics.
 *
 * Buffer lifecycle:
 *   1. vrtd_buffer_open()          -- daemon allocates, returns qpair fd
 *   2. vrtd_buffer_create_raw()    -- client mmaps host memory
 *   3. vrtd_buffer_sync_to/from_device() -- DMA transfers
 *   4. vrtd_buffer_close()         -- tells daemon to free, unmaps locally
 */

#define _GNU_SOURCE

#include <vrtd/vrtd.h>

#include "v80_policy.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>


#include <stdio.h>

#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT 26
#endif

#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21UL << MAP_HUGE_SHIFT)
#endif

#define BASE_TRANSFER_STEP_SIZE (4ULL * 1024ULL)              // 4K
#define HUGE_TRANSFER_STEP_SIZE (2ULL * 1024ULL * 1024ULL)    // 2M

/*
 * Per-sync timing instrumentation.
 *
 * When SLASH_QDMA_TIMING is non-zero (compile-time flag, e.g. built with
 * -DSLASH_QDMA_TIMING=1), the sync_to/from_device paths log the wall-clock
 * cost of each transfer ioctl plus the aggregate per-sync time and
 * effective bandwidth.  This is the userspace counterpart to the kernel's
 * SLASH_QDMA_TIMING and libqdma's QDMA_TIMING breakdowns.
 */
#ifndef SLASH_QDMA_TIMING
#define SLASH_QDMA_TIMING 0
#endif

#if SLASH_QDMA_TIMING
static inline uint64_t vrtd_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}
#endif

static void vrtd_prefault_mapping(void *addr, uint64_t size) {
    volatile uint8_t *touch = (volatile uint8_t *) addr;

    for (uint64_t off = 0; off < size; off += BASE_TRANSFER_STEP_SIZE) {
        touch[off] = 0;
    }
}

static int vrtd_mmap_regular_base_pages(uint64_t size, void **addr_out) {
    void *addr;

    if (addr_out == NULL || size == 0) {
        return -EINVAL;
    }

    addr = mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (addr == MAP_FAILED) {
        return -errno;
    }

    if (madvise(addr, size, MADV_NOHUGEPAGE) != 0) {
        int saved_errno = errno;
        (void) munmap(addr, size);
        return -saved_errno;
    }

    vrtd_prefault_mapping(addr, size);
    *addr_out = addr;
    return 0;
}

/*
 * Issue a single contiguous transfer of [buf_offset, buf_offset + size) on one
 * qpair fd.  The QDMA transfer ioctl operates on signed ssize_t lengths, so the
 * range is chunked to stay within SSIZE_MAX while preserving step alignment.
 */
static int vrtd_transfer_segment(
    int qpair_fd,
    uint32_t buf_id,
    uint64_t buf_offset,
    uint64_t phys_addr,
    uint64_t size,
    uint64_t step,
    uint32_t direction
) {
    uint64_t max_chunk = (uint64_t)SSIZE_MAX - ((uint64_t)SSIZE_MAX % step);
    uint64_t done = 0;

    if (max_chunk == 0) {
        return -EINVAL;
    }

    while (done < size) {
        uint64_t remaining = size - done;
        uint64_t chunk = remaining > max_chunk ? max_chunk : remaining;
        uint64_t xfer_offset = buf_offset + done;
        uint64_t dev_offset = phys_addr + xfer_offset;
        ssize_t ret = slash_qdma_qpair_transfer(
            qpair_fd, buf_id, xfer_offset, dev_offset, chunk, direction);

        if (ret <= 0) {
            return -EIO;
        }
        done += (uint64_t) ret;
    }

    return 0;
}

static int vrtd_transfer_registered(
    const int *qpair_fds,
    uint32_t qpair_fd_count,
    enum slash_qdma_transfer_hint transfer_hint,
    uint32_t buf_id,
    uint64_t phys_addr,
    uint64_t offset,
    uint64_t size,
    uint64_t step,
    bool to_device
) {
    uint32_t direction = to_device ? SLASH_QDMA_XFER_H2C : SLASH_QDMA_XFER_C2H;

    if (size == 0) {
        return 0;
    }

    if (qpair_fds == NULL || qpair_fd_count == 0 || qpair_fds[0] < 0) {
        return -EINVAL;
    }

    if (step == 0 || (offset % step) != 0 || (size % step) != 0) {
        return -EINVAL;
    }

    /*
     * Decide how the transfer maps onto the available queues.  V80 applies the
     * placement-aware policy (DDR halved, HBM routed by the half-memory
     * boundary); any other hint keeps everything on the primary qpair.
     */
    struct vrtd_xfer_seg segs[VRTD_V80_MAX_SEGS];
    uint32_t nseg;

    if (transfer_hint == SLASH_QDMA_TRANSFER_HINT_V80) {
        nseg = vrtd_plan_v80(phys_addr, offset, size, step, qpair_fd_count, segs);
    } else {
        segs[0].fd_index = 0;
        segs[0].offset = offset;
        segs[0].size = size;
        nseg = 1;
    }

    for (uint32_t i = 0; i < nseg; ++i) {
        uint32_t fd_index = segs[i].fd_index;

        /* The plan only references fds[0]/fds[1]; fall back to the primary
         * qpair if a planned fd is somehow unavailable. */
        if (fd_index >= qpair_fd_count || qpair_fds[fd_index] < 0) {
            fd_index = 0;
        }

        int ret = vrtd_transfer_segment(
            qpair_fds[fd_index], buf_id, segs[i].offset,
            phys_addr, segs[i].size, step, direction);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

static int vrtd_transfer_temporary_mapping(
    const struct vrtd_buffer *buffer,
    void *mapping,
    uint64_t phys_addr,
    uint64_t size,
    bool to_device
) {
    uint32_t buf_id = 0;
    enum slash_qdma_transfer_hint hint = SLASH_QDMA_TRANSFER_HINT_SINGLE_QPAIR;
    int ret;

    if (buffer == NULL || mapping == NULL || buffer->qpair_fd_count == 0) {
        return -EINVAL;
    }

    if (slash_qdma_qpair_buffer_register(buffer->qpair_fds[0], mapping, size,
                                         &buf_id, &hint) != 0) {
        return -EIO;
    }

    ret = vrtd_transfer_registered(buffer->qpair_fds, buffer->qpair_fd_count,
                                   hint, buf_id, phys_addr, 0, size,
                                   BASE_TRANSFER_STEP_SIZE, to_device);

    (void)slash_qdma_qpair_buffer_unregister(buffer->qpair_fds[0], buf_id);
    return ret;
}

enum vrtd_ret vrtd_buffer_create_raw(
    int sock_fd,
    uint32_t dev,
    uint32_t alloc_type,
    uint32_t alloc_dir,
    uint64_t alloc_arg,
    uint64_t size,
    uint64_t phys_addr,
    const int *qpair_fds,
    uint32_t qpair_fd_count,
    enum vrtd_host_page_mode page_mode,
    struct vrtd_buffer **buffer_out
) {
    if (buffer_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_buffer *buffer = (struct vrtd_buffer *) malloc(sizeof(struct vrtd_buffer));
    if (buffer == NULL) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    buffer->buf = MAP_FAILED;
    buffer->transfer_step_size = BASE_TRANSFER_STEP_SIZE;
    buffer->qpair_fds[0] = -1;
    buffer->qpair_fds[1] = -1;
    buffer->qpair_fd_count = 0;
    buffer->buf_id = 0;
    buffer->transfer_hint = SLASH_QDMA_TRANSFER_HINT_SINGLE_QPAIR;

    if (qpair_fds == NULL || qpair_fd_count == 0 || qpair_fd_count > 2 || qpair_fds[0] < 0) {
        free(buffer);
        return VRTD_RET_BAD_LIB_CALL;
    }

    if (page_mode == VRTD_HOST_PAGE_2M) {
        /*
         * Explicit 2 MiB hugetlb request: there is no fallback.  The DMA
         * granule and the device address must both be 2 MiB aligned, and the
         * hugetlb mapping must succeed, otherwise the allocation fails so the
         * caller can react instead of silently transferring over 4 KiB pages.
         */
        if ((size % HUGE_TRANSFER_STEP_SIZE) != 0 ||
            (phys_addr % HUGE_TRANSFER_STEP_SIZE) != 0) {
            free(buffer);
            return VRTD_RET_INVALID_ARGUMENT;
        }

        buffer->buf = mmap(
            NULL, /* address (let the kernel choose) */
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB | MAP_POPULATE,
            -1, /* fd */
            0   /* offset */
        );
        if (buffer->buf == MAP_FAILED) {
            int huge_errno = errno;
            syslog(
                LOG_ERR,
                "libvrtd: 2 MiB hugetlb mapping failed for buffer size=%llu phys_addr=0x%llx errno=%d; "
                "reserve 2 MiB hugepages or request 4 KiB pages",
                (unsigned long long)size,
                (unsigned long long)phys_addr,
                huge_errno
            );
            free(buffer);
            return VRTD_RET_INTERNAL_ERROR;
        }
        buffer->transfer_step_size = HUGE_TRANSFER_STEP_SIZE;
#if SLASH_QDMA_TIMING
        syslog(
            LOG_INFO,
            "libvrtd: buffer host mapping path=hugetlb-2m size=%llu phys_addr=0x%llx step=%llu",
            (unsigned long long)size,
            (unsigned long long)phys_addr,
            (unsigned long long)buffer->transfer_step_size
        );
#endif
    } else {
        /*
         * Explicit 4 KiB base-page request.  Do not use MAP_POPULATE before
         * MADV_NOHUGEPAGE: THP=always can fault compound pages before the
         * advice takes effect, and the kernel QDMA base-page path intentionally
         * rejects those pages (vrtd_mmap_regular_base_pages handles this).
         */
        int mmap_ret = vrtd_mmap_regular_base_pages(size, &buffer->buf);
        if (mmap_ret != 0) {
            free(buffer);
            return VRTD_RET_INTERNAL_ERROR;
        }
        buffer->transfer_step_size = BASE_TRANSFER_STEP_SIZE;
#if SLASH_QDMA_TIMING
        syslog(
            LOG_INFO,
            "libvrtd: buffer host mapping path=regular-4k size=%llu phys_addr=0x%llx step=%llu",
            (unsigned long long)size,
            (unsigned long long)phys_addr,
            (unsigned long long)buffer->transfer_step_size
        );
#endif
    }

    buffer->sock_fd    = sock_fd;
    buffer->dev        = dev;
    buffer->alloc_type = alloc_type;
    buffer->alloc_dir  = alloc_dir;
    buffer->alloc_arg  = alloc_arg;
    buffer->size       = size;
    buffer->phys_addr  = phys_addr;
    buffer->qpair_fd_count = qpair_fd_count;
    for (uint32_t i = 0; i < qpair_fd_count; ++i) {
        buffer->qpair_fds[i] = qpair_fds[i];
    }

    if (slash_qdma_qpair_buffer_register(
            buffer->qpair_fds[0], buffer->buf, buffer->size,
            &buffer->buf_id, &buffer->transfer_hint) != 0) {
        (void) munmap(buffer->buf, buffer->size);
        free(buffer);
        return VRTD_RET_INTERNAL_ERROR;
    }

    *buffer_out = buffer;

    return VRTD_RET_OK;
}

static enum vrtd_ret vrtd_buffer_prepare_sync_range(
    const struct vrtd_buffer *buffer,
    uint64_t offset,
    uint64_t size,
    uint64_t *aligned_offset_out,
    uint64_t *aligned_size_out,
    bool *needs_bounce_out
) {
    uint64_t step;
    uint64_t end;
    uint64_t aligned_offset;
    uint64_t aligned_end;

    if (buffer == NULL || aligned_offset_out == NULL ||
        aligned_size_out == NULL || needs_bounce_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    step = buffer->transfer_step_size;
    if (step == 0) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    if (offset > buffer->size || size > buffer->size - offset) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    if (size == 0) {
        *aligned_offset_out = offset;
        *aligned_size_out = 0;
        *needs_bounce_out = false;
        return VRTD_RET_OK;
    }

    if ((buffer->size % step) != 0 || (buffer->phys_addr % step) != 0) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    end = offset + size;
    aligned_offset = offset - (offset % step);
    if (end > UINT64_MAX - (step - 1)) {
        return VRTD_RET_INVALID_ARGUMENT;
    }
    aligned_end = ((end + step - 1) / step) * step;
    if (aligned_end > buffer->size) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    *aligned_offset_out = aligned_offset;
    *aligned_size_out = aligned_end - aligned_offset;
    *needs_bounce_out = (aligned_offset != offset || aligned_end != end);

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_buffer_destroy(
    struct vrtd_buffer *buffer
) {
    if (buffer == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    for (uint32_t i = 0; i < buffer->qpair_fd_count && i < 2; ++i) {
        if (buffer->qpair_fds[i] >= 0) {
            (void) slash_qdma_qpair_buffer_unregister(buffer->qpair_fds[i], buffer->buf_id);
            break;
        }
    }

    for (uint32_t i = 0; i < buffer->qpair_fd_count && i < 2; ++i) {
        if (buffer->qpair_fds[i] >= 0) {
            (void) close(buffer->qpair_fds[i]);
            buffer->qpair_fds[i] = -1;
        }
    }

    if (buffer->buf != NULL) {
        (void) munmap(buffer->buf, buffer->size);
    }

    free(buffer);

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_buffer_close(
    struct vrtd_buffer *buffer
)
{
    if (buffer == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_req_buffer_close req = {
        .dev_number = buffer->dev,
        .phys_addr = buffer->phys_addr,
        .size = buffer->size,
    };
    struct vrtd_resp_buffer_close resp = {0};

    enum vrtd_ret ret = vrtd_raw_request(
        buffer->sock_fd,
        VRTD_REQ_BUFFER_CLOSE,
        &req,
        sizeof(req),
        &resp,
        sizeof(resp),
        NULL,
        NULL
    );

    enum vrtd_ret destroy_ret = vrtd_buffer_destroy(buffer);
    if (ret != VRTD_RET_OK) {
        return ret;
    }
    return destroy_ret;
}

enum vrtd_ret vrtd_buffer_sync_to_device(
    struct vrtd_buffer *buffer,
    uint64_t offset,
    uint64_t size
) {
    if (buffer == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    if (buffer->alloc_dir == VRTD_ALLOC_DIR_DEVICE_TO_HOST) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    assert(buffer->qpair_fd_count > 0);
    assert(buffer->qpair_fds[0] >= 0);
    assert(buffer->buf != NULL);
    uint64_t aligned_offset = 0;
    uint64_t aligned_size = 0;
    bool needs_bounce = false;
    enum vrtd_ret range_ret = vrtd_buffer_prepare_sync_range(
        buffer, offset, size, &aligned_offset, &aligned_size, &needs_bounce);
    if (range_ret != VRTD_RET_OK) {
        return range_ret;
    }
    if (aligned_size == 0) {
        return VRTD_RET_OK;
    }

    uint64_t step = buffer->transfer_step_size;
#if SLASH_QDMA_TIMING
    uint64_t sync_start_ns = vrtd_now_ns();
#endif

    int transfer_ret;
    if (needs_bounce && buffer->alloc_dir == VRTD_ALLOC_DIR_BIDIRECTIONAL) {
        void *bounce = NULL;
        int mmap_ret = vrtd_mmap_regular_base_pages(aligned_size, &bounce);
        if (mmap_ret != 0) {
            return VRTD_RET_INTERNAL_ERROR;
        }

        transfer_ret = vrtd_transfer_temporary_mapping(
            buffer, bounce, buffer->phys_addr + aligned_offset,
            aligned_size, false);
        if (transfer_ret == 0) {
            memcpy(
                (uint8_t *)bounce + (offset - aligned_offset),
                (uint8_t *)buffer->buf + offset,
                size
            );
            transfer_ret = vrtd_transfer_temporary_mapping(
                buffer, bounce, buffer->phys_addr + aligned_offset,
                aligned_size, true);
        }
        (void) munmap(bounce, aligned_size);
    } else {
        /*
         * Host-to-device-only buffers cannot read the surrounding device
         * granule for a read-modify-write, so keep the historical behavior:
         * expand partial syncs to the backing DMA granule.
         */
        transfer_ret = vrtd_transfer_registered(
            buffer->qpair_fds, buffer->qpair_fd_count, buffer->transfer_hint,
            buffer->buf_id, buffer->phys_addr,
            aligned_offset, aligned_size, step, true);
    }
    if (transfer_ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

#if SLASH_QDMA_TIMING
    {
        uint64_t total_ns = vrtd_now_ns() - sync_start_ns;
        double mb = (double) size / (1024.0 * 1024.0);
        double sec = (double) total_ns / 1e9;
        syslog(LOG_INFO,
               "libvrtd: timing H2C sync offset=%llu size=%llu aligned_offset=%llu aligned_size=%llu step=%llu total=%llu ns (%.1f MB/s)",
               (unsigned long long) offset, (unsigned long long) size,
               (unsigned long long) aligned_offset, (unsigned long long) aligned_size,
               (unsigned long long) step, (unsigned long long) total_ns,
               sec > 0.0 ? mb / sec : 0.0);
    }
#endif

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_buffer_sync_from_device(
    struct vrtd_buffer *buffer,
    uint64_t offset,
    uint64_t size
) {
    if (buffer == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    if (buffer->alloc_dir == VRTD_ALLOC_DIR_HOST_TO_DEVICE) {
        return VRTD_RET_INVALID_ARGUMENT;
    }

    assert(buffer->qpair_fd_count > 0);
    assert(buffer->qpair_fds[0] >= 0);
    assert(buffer->buf != NULL);
    uint64_t aligned_offset = 0;
    uint64_t aligned_size = 0;
    bool needs_bounce = false;
    enum vrtd_ret range_ret = vrtd_buffer_prepare_sync_range(
        buffer, offset, size, &aligned_offset, &aligned_size, &needs_bounce);
    if (range_ret != VRTD_RET_OK) {
        return range_ret;
    }
    if (aligned_size == 0) {
        return VRTD_RET_OK;
    }

    uint64_t step = buffer->transfer_step_size;
#if SLASH_QDMA_TIMING
    uint64_t sync_start_ns = vrtd_now_ns();
#endif

    int transfer_ret;
    if (needs_bounce) {
        void *bounce = NULL;
        int mmap_ret = vrtd_mmap_regular_base_pages(aligned_size, &bounce);
        if (mmap_ret != 0) {
            return VRTD_RET_INTERNAL_ERROR;
        }

        transfer_ret = vrtd_transfer_temporary_mapping(
            buffer, bounce, buffer->phys_addr + aligned_offset,
            aligned_size, false);
        if (transfer_ret == 0) {
            memcpy(
                (uint8_t *)buffer->buf + offset,
                (uint8_t *)bounce + (offset - aligned_offset),
                size
            );
        }
        (void) munmap(bounce, aligned_size);
    } else {
        transfer_ret = vrtd_transfer_registered(
            buffer->qpair_fds, buffer->qpair_fd_count, buffer->transfer_hint,
            buffer->buf_id, buffer->phys_addr,
            aligned_offset, aligned_size, step, false);
    }
    if (transfer_ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

#if SLASH_QDMA_TIMING
    {
        uint64_t total_ns = vrtd_now_ns() - sync_start_ns;
        double mb = (double) size / (1024.0 * 1024.0);
        double sec = (double) total_ns / 1e9;
        syslog(LOG_INFO,
               "libvrtd: timing C2H sync offset=%llu size=%llu aligned_offset=%llu aligned_size=%llu step=%llu total=%llu ns (%.1f MB/s)",
               (unsigned long long) offset, (unsigned long long) size,
               (unsigned long long) aligned_offset, (unsigned long long) aligned_size,
               (unsigned long long) step, (unsigned long long) total_ns,
               sec > 0.0 ? mb / sec : 0.0);
    }
#endif

    return VRTD_RET_OK;
}
