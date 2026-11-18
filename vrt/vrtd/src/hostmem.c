/**
 * The MIT License (MIT)
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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
 * @file hostmem.c
 *
 * Host buffers through the sl_vmem translator and the slave-bridge windows
 * (see hostmem.h).  Register map: nic/rtl/sl_vmem.sv.
 */

#define _GNU_SOURCE

#include "hostmem.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

/* sl_vmem registers (byte offsets from HOSTMEM_SLV_OFFSET) */
#define SLV_ID          0x00u
#define SLV_VERSION     0x04u
#define SLV_CAPS        0x0Cu
#define SLV_CTRL        0x10u
#define SLV_STATUS      0x14u
#define SLV_WIN_LIMIT   0x18u
#define SLV_OUTSTANDING 0x1Cu
#define SLV_W_PENDING   0x20u
#define SLV_CLEAR       0x24u
#define SLV_FAULT_ADDR  0x28u
#define SLV_FAULT_INFO  0x30u
#define SLV_FAULT_COUNT 0x34u
#define SLV_WIN_BASE    0x40u
#define SLV_VA_BASE     0x48u
#define SLV_TABLE       0x10000u

#define SLV_ID_VALUE      0x534C564Du   /* "SLVM" */
#define SLV_VERSION_MAJOR 0x0001u

#define CTRL_EN     0x1u
#define CTRL_PAUSE  0x2u
#define ST_INIT_BUSY 0x1u
#define ST_DRAINED   0x2u
#define CLEAR_FAULT    0x1u
#define CLEAR_COUNTERS 0x2u

#define E_VALID 0x1u
#define E_R     0x2u
#define E_W     0x4u

#define DRAIN_TIMEOUT_NS (100ull * 1000ull * 1000ull)

/* ---- register access ----------------------------------------------------- */

static volatile uint32_t *slv_regs(const struct hostmem *hm)
{
    const struct slash_bar_file *bar = *hm->service_bar;

    if (bar == NULL || bar->map == NULL || bar->len < HOSTMEM_SLV_OFFSET + SLV_TABLE + 4ull * 16384u) {
        return NULL;
    }
    return (volatile uint32_t *) ((uint8_t *) bar->map + HOSTMEM_SLV_OFFSET);
}

static uint32_t rd(volatile uint32_t *r, uint32_t off)
{
    return r[off / 4];
}

static void wr(volatile uint32_t *r, uint32_t off, uint32_t v)
{
    r[off / 4] = v;
}

static uint64_t rd64(volatile uint32_t *r, uint32_t off)
{
    return (uint64_t) rd(r, off) | ((uint64_t) rd(r, off + 4) << 32);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

/**
 * Pause the translator and wait until nothing is in flight: new requests
 * stall at its input (a running kernel just waits), so the table and the BDF
 * entries can change underneath.
 */
static bool slv_pause(volatile uint32_t *r)
{
    wr(r, SLV_CTRL, rd(r, SLV_CTRL) | CTRL_PAUSE);
    uint64_t end = now_ns() + DRAIN_TIMEOUT_NS;
    do {
        if ((rd(r, SLV_STATUS) & ST_DRAINED) && rd(r, SLV_OUTSTANDING) == 0 && rd(r, SLV_W_PENDING) == 0) {
            return true;
        }
        struct timespec ts = { 0, 10000 };
        nanosleep(&ts, NULL);
    } while (now_ns() < end);
    LOG(LOG_WARNING, "hostmem: translator did not drain (outstanding 0x%08x, W pending %u)",
        rd(r, SLV_OUTSTANDING), rd(r, SLV_W_PENDING));
    return false;
}

static void slv_resume(volatile uint32_t *r)
{
    wr(r, SLV_CTRL, rd(r, SLV_CTRL) & ~CTRL_PAUSE);
}

/* ---- buffers ------------------------------------------------------------- */

void cleanup_host_buffer(struct host_buffer *hb)
{
    if (hb == NULL) {
        return;
    }
    if (hb->fd >= 0) {
        (void) close(hb->fd);
    }
    free(hb);
}

/**
 * Invalidate the buffer's table entries, then clear its windows (the driver
 * drops its buffer reference only after the BDF entry is clear), with the
 * translator paused.  Leaves the buffer struct to the caller.
 */
static void hostmem_unmap_buffer(struct hostmem *hm, struct host_buffer *hb)
{
    volatile uint32_t *r = slv_regs(hm);

    if (r != NULL) {
        bool drained = slv_pause(r);
        for (uint32_t i = 0; i < hb->mapped; i++) {
            wr(r, SLV_TABLE + 4u * (hb->first_vpn + i), 0);
        }
        if (!drained) {
            LOG(LOG_WARNING, "hostmem: clearing the windows of buffer 0x%llx with traffic in flight",
                (unsigned long long) hb->card_addr);
        }
    }
    for (uint32_t i = 0; i < hb->mapped; i++) {
        if (slash_qdma_slv_unmap(hm->qdma, hb->wins[i]) != 0) {
            LOG(LOG_ERR, "hostmem: SLV_UNMAP window %u: %m", (unsigned int) hb->wins[i]);
        }
        hm->win_used &= ~(1ull << hb->wins[i]);
    }
    for (uint32_t i = 0; i < hb->npages; i++) {
        hm->vpn_used[hb->first_vpn + i] = 0;
    }
    if (r != NULL) {
        slv_resume(r);
    }
    hb->mapped = 0;
}

static void hostmem_drop_buffer(struct hostmem *hm, struct host_buffer *hb)
{
    hostmem_unmap_buffer(hm, hb);
    LOG(LOG_INFO, "hostmem: closed host buffer card_addr=0x%llx size=%llu conn_id=%llu",
        (unsigned long long) hb->card_addr, (unsigned long long) hb->size,
        (unsigned long long) hb->client_id);
    host_buffer_ptr_array_rm_by_reference(&hm->buffers, hb);   /* closes the fd, frees */
}

/* ---- manager ------------------------------------------------------------- */

bool hostmem_present(const struct hostmem *hm)
{
    volatile uint32_t *r = slv_regs(hm);
    return r != NULL && rd(r, SLV_ID) == SLV_ID_VALUE;
}

struct hostmem *hostmem_open(struct slash_qdma *qdma, struct slash_bar_file **service_bar)
{
    struct slash_qdma_slv_info info;
    struct hostmem probe = { .qdma = qdma, .service_bar = service_bar };
    volatile uint32_t *r = slv_regs(&probe);

    if (qdma == NULL || r == NULL || rd(r, SLV_ID) != SLV_ID_VALUE ||
        (rd(r, SLV_VERSION) >> 16) != SLV_VERSION_MAJOR) {
        errno = ENODEV;
        return NULL;
    }
    if (slash_qdma_slv_info(qdma, &info) != 0 || !info.present) {
        LOG(LOG_NOTICE, "hostmem: sl_vmem present but the slash driver has no slave-bridge windows");
        errno = ENODEV;
        return NULL;
    }

    uint32_t caps = rd(r, SLV_CAPS);
    uint32_t nvpn = caps & 0xFFFFu;
    uint32_t page_shift = (caps >> 16) & 0xFFu;
    uint32_t hw_windows = caps >> 24;
    if (nvpn == 0 || nvpn > 16384u || page_shift != 21 || info.window_size != (1ull << page_shift) ||
        rd64(r, SLV_WIN_BASE) != info.axi_base) {
        LOG(LOG_ERR, "hostmem: translator caps 0x%08x / window base 0x%llx do not match the driver's windows",
            caps, (unsigned long long) rd64(r, SLV_WIN_BASE));
        errno = ENODEV;
        return NULL;
    }

    struct hostmem *hm = calloc(1, sizeof(*hm));
    if (hm == NULL) {
        return NULL;
    }
    hm->vpn_used = calloc(nvpn, 1);
    if (hm->vpn_used == NULL) {
        free(hm);
        return NULL;
    }
    hm->qdma = qdma;
    hm->service_bar = service_bar;
    hm->nvpn = nvpn;
    hm->page_size = 1u << page_shift;
    hm->va_base = rd64(r, SLV_VA_BASE);
    hm->n_windows = info.n_windows < hw_windows ? info.n_windows : hw_windows;
    if (hm->n_windows > SLASH_QDMA_SLV_WINDOWS) {
        hm->n_windows = SLASH_QDMA_SLV_WINDOWS;
    }
    hm->iommu = info.iommu != 0;
    /* Windows already mapped belong to an earlier vrtd fd (the driver clears
     * those on close, so this is only another tool); never touch them. */
    hm->win_used = info.used_mask;

    /* Reset: wait for the table init, pause, every entry invalid, enable. */
    uint64_t end = now_ns() + DRAIN_TIMEOUT_NS;
    while ((rd(r, SLV_STATUS) & ST_INIT_BUSY) && now_ns() < end) {
    }
    (void) slv_pause(r);
    for (uint32_t v = 0; v < nvpn; v++) {
        wr(r, SLV_TABLE + 4u * v, 0);
    }
    wr(r, SLV_WIN_LIMIT, hm->n_windows);
    wr(r, SLV_CLEAR, CLEAR_FAULT | CLEAR_COUNTERS);
    wr(r, SLV_CTRL, CTRL_EN);

    LOG(LOG_INFO, "hostmem: translator ready: %u pages of %u KiB at card address 0x%llx, %u windows,"
        " IOMMU %s", nvpn, hm->page_size >> 10, (unsigned long long) hm->va_base, hm->n_windows,
        hm->iommu ? "translating" : "off/pass-through");
    return hm;
}

void cleanup_hostmem(struct hostmem *hm)
{
    if (hm == NULL) {
        return;
    }
    while (hm->buffers.len > 0) {
        hostmem_drop_buffer(hm, hm->buffers.d[hm->buffers.len - 1]);
    }
    host_buffer_ptr_array_free(&hm->buffers);
    volatile uint32_t *r = slv_regs(hm);
    if (r != NULL && rd(r, SLV_ID) == SLV_ID_VALUE) {
        wr(r, SLV_CTRL, 0);
    }
    free(hm->vpn_used);
    free(hm);
}

void hostmem_info(struct hostmem *hm, struct vrtd_resp_host_mem_info *resp)
{
    volatile uint32_t *r = slv_regs(hm);

    memset(resp, 0, sizeof(*resp));
    resp->present = (r != NULL && rd(r, SLV_ID) == SLV_ID_VALUE) ? 1 : 0;
    resp->iommu = hm->iommu ? 1 : 0;
    resp->windows_total = hm->n_windows;
    uint32_t used = 0;
    for (uint32_t w = 0; w < hm->n_windows; w++) {
        used += (hm->win_used >> w) & 1u;
    }
    resp->windows_free = hm->n_windows - used;
    resp->page_size = hm->page_size;
    resp->va_base = hm->va_base;
    resp->va_size = (uint64_t) hm->nvpn * hm->page_size;
    if (resp->present) {
        resp->fault_count = rd(r, SLV_FAULT_COUNT);
        resp->fault_info = rd(r, SLV_FAULT_INFO);
        resp->fault_addr = rd64(r, SLV_FAULT_ADDR);
    }
}

uint16_t hostmem_buffer_open(struct hostmem *hm, uint64_t client_id, uint32_t flags, uint64_t size,
                             int *fd_out, uint64_t *card_addr_out, uint64_t *size_out)
{
    volatile uint32_t *r = slv_regs(hm);

    if (r == NULL || rd(r, SLV_ID) != SLV_ID_VALUE) {
        return VRTD_RET_NOEXIST;
    }
    if (flags == 0) {
        flags = VRTD_HOST_BUFFER_READ | VRTD_HOST_BUFFER_WRITE;
    }
    if (size == 0 || (flags & ~(VRTD_HOST_BUFFER_READ | VRTD_HOST_BUFFER_WRITE)) != 0) {
        return VRTD_RET_INVALID_ARGUMENT;
    }
    uint64_t npages64 = (size + hm->page_size - 1) / hm->page_size;
    if (npages64 > hm->n_windows) {
        LOG(LOG_NOTICE, "hostmem: %llu pages requested, %u windows exist",
            (unsigned long long) npages64, hm->n_windows);
        return VRTD_RET_INVALID_ARGUMENT;
    }
    uint32_t npages = (uint32_t) npages64;

    /* windows: any free ones */
    uint8_t wins[SLASH_QDMA_SLV_WINDOWS];
    uint32_t nw = 0;
    for (uint32_t w = 0; w < hm->n_windows && nw < npages; w++) {
        if (!((hm->win_used >> w) & 1u)) {
            wins[nw++] = (uint8_t) w;
        }
    }
    if (nw < npages) {
        return VRTD_RET_BUSY;
    }
    /* VA: first fit, contiguous */
    uint32_t first = 0, run = 0;
    for (uint32_t v = 0; v < hm->nvpn && run < npages; v++) {
        if (hm->vpn_used[v]) {
            run = 0;
            first = v + 1;
        } else {
            run++;
        }
    }
    if (run < npages) {
        return VRTD_RET_BUSY;
    }

    struct host_buffer *hb = calloc(1, sizeof(*hb));
    if (hb == NULL) {
        return VRTD_RET_INTERNAL_ERROR;
    }
    hb->client_id = client_id;
    hb->first_vpn = first;
    hb->npages = npages;
    hb->size = (uint64_t) npages * hm->page_size;
    hb->card_addr = hm->va_base + (uint64_t) first * hm->page_size;
    hb->perm = flags;
    memcpy(hb->wins, wins, npages);
    hb->fd = slash_qdma_slv_buffer_create(hm->qdma, hb->size);
    if (hb->fd < 0) {
        int err = errno;
        LOG(LOG_ERR, "hostmem: SLV_BUF_CREATE %llu bytes: %m", (unsigned long long) hb->size);
        free(hb);
        return err == ENOMEM ? VRTD_RET_BUSY : VRTD_RET_INTERNAL_ERROR;
    }
    for (uint32_t i = 0; i < npages; i++) {
        hm->vpn_used[first + i] = 1;
    }

    /* windows, then table entries, with the translator paused */
    uint32_t entry_perm = E_VALID | ((flags & VRTD_HOST_BUFFER_READ) ? E_R : 0) |
                          ((flags & VRTD_HOST_BUFFER_WRITE) ? E_W : 0);
    uint16_t ret = VRTD_RET_OK;
    bool drained = slv_pause(r);
    if (!drained) {
        ret = VRTD_RET_BUSY;
    }
    for (uint32_t i = 0; ret == VRTD_RET_OK && i < npages; i++) {
        if (slash_qdma_slv_map(hm->qdma, wins[i], hb->fd, (uint64_t) i * hm->page_size, NULL) != 0) {
            LOG(LOG_ERR, "hostmem: SLV_MAP window %u: %m", (unsigned int) wins[i]);
            ret = errno == EBUSY ? VRTD_RET_BUSY : VRTD_RET_INTERNAL_ERROR;
            break;
        }
        hm->win_used |= 1ull << wins[i];
        uint32_t e = entry_perm | ((uint32_t) wins[i] << 8);
        wr(r, SLV_TABLE + 4u * (first + i), e);
        hb->mapped = i + 1;
        if (rd(r, SLV_TABLE + 4u * (first + i)) != e) {
            LOG(LOG_ERR, "hostmem: table entry %u did not read back", first + i);
            ret = VRTD_RET_INTERNAL_ERROR;
        }
    }
    slv_resume(r);

    if (ret == VRTD_RET_OK && host_buffer_ptr_array_push(&hm->buffers, hb) != 0) {
        ret = VRTD_RET_INTERNAL_ERROR;
    }
    if (ret != VRTD_RET_OK) {
        hostmem_unmap_buffer(hm, hb);
        cleanup_host_buffer(hb);
        return ret;
    }

    *fd_out = hb->fd;
    *card_addr_out = hb->card_addr;
    *size_out = hb->size;
    LOG(LOG_INFO, "hostmem: opened host buffer card_addr=0x%llx size=%llu perm=%s%s windows %u.. conn_id=%llu",
        (unsigned long long) hb->card_addr, (unsigned long long) hb->size,
        (flags & VRTD_HOST_BUFFER_READ) ? "r" : "-", (flags & VRTD_HOST_BUFFER_WRITE) ? "w" : "-",
        (unsigned int) wins[0], (unsigned long long) client_id);
    return VRTD_RET_OK;
}

uint16_t hostmem_buffer_close(struct hostmem *hm, uint64_t client_id, uint64_t card_addr)
{
    for (size_t i = 0; i < hm->buffers.len; i++) {
        struct host_buffer *hb = hm->buffers.d[i];
        if (hb->card_addr != card_addr) {
            continue;
        }
        if (hb->client_id != client_id) {
            return VRTD_RET_AUTH_ERROR;
        }
        hostmem_drop_buffer(hm, hb);
        return VRTD_RET_OK;
    }
    return VRTD_RET_NOEXIST;
}

void hostmem_cleanup_client(struct hostmem *hm, uint64_t client_id)
{
    size_t i = 0;
    while (i < hm->buffers.len) {
        struct host_buffer *hb = hm->buffers.d[i];
        if (hb->client_id == client_id) {
            hostmem_drop_buffer(hm, hb);   /* the next buffer slides into slot i */
        } else {
            i++;
        }
    }
}
