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
 * @file hostmem.h
 *
 * Host buffers: host memory a user-region kernel reaches through the
 * service-layer translator (sl_vmem) and the CPM5 slave bridge.
 *
 *   kernel m_axi -> card address (aperture va_base + VA)
 *     -> sl_vmem table entry VA >> 21: valid, R/W, window w      (vrtd, service BAR)
 *     -> slave-bridge window w (AXI 0xE800_0000 + w * 2 MiB)
 *     -> BDF entry w: host DMA address of a 2 MiB buffer chunk   (slash driver)
 *
 * vrtd owns the translator: clients never get the service BAR, so the only
 * table entries are the ones written here, each pointing at a window the
 * driver mapped onto a live buffer of the same client.  Every change to the
 * table or the BDF entries happens with the translator paused and drained.
 */

#ifndef VRTD_HOSTMEM_H
#define VRTD_HOSTMEM_H

#include <stdbool.h>
#include <stdint.h>

#include <slash/ctldev.h>
#include <slash/qdma.h>
#include <vrtd/wire.h>

#include "array.h"

/** sl_vmem's offset in the service BAR (slvmem service-layer RM). */
#define HOSTMEM_SLV_OFFSET 0x04000000u

struct host_buffer {
    uint64_t client_id;    /**< Owning connection id. */
    int fd;                /**< vrtd's copy of the driver buffer fd (-1 when closed). */
    uint64_t card_addr;    /**< Card address of byte 0. */
    uint64_t size;         /**< Bytes (npages * page size). */
    uint32_t first_vpn;    /**< First translator page. */
    uint32_t npages;       /**< Translator pages / windows. */
    uint32_t perm;         /**< VRTD_HOST_BUFFER_READ | _WRITE. */
    uint32_t mapped;       /**< Pages whose window and table entry are set. */
    uint8_t wins[SLASH_QDMA_SLV_WINDOWS]; /**< Window of page i. */
};

void cleanup_host_buffer(struct host_buffer *hb);

DECLARE_OWNING_PTR_ARRAY(host_buffer_ptr_array, struct host_buffer *, cleanup_host_buffer);

struct hostmem {
    struct slash_qdma *qdma;                /**< Borrowed from the device. */
    struct slash_bar_file **service_bar;    /**< The device's BAR 2 slot (re-read on every access). */
    uint32_t nvpn;                          /**< Translator pages. */
    uint32_t page_size;                     /**< Bytes per page / window. */
    uint64_t va_base;                       /**< Card address of the aperture. */
    uint32_t n_windows;                     /**< Windows vrtd may use (0..n-1). */
    bool iommu;                             /**< IOMMU translates host DMA addresses. */
    uint64_t win_used;                      /**< Bit w: window w in use. */
    uint8_t *vpn_used;                      /**< nvpn flags. */
    struct host_buffer_ptr_array buffers;   /**< Open host buffers. */
};

/**
 * Probe for the translator and the driver's windows, and reset the
 * translator (every entry invalid, faults cleared, enabled).
 *
 * @return The manager, or NULL with errno ENODEV (no translator / windows)
 *         or another errno on failure.
 */
struct hostmem *hostmem_open(struct slash_qdma *qdma, struct slash_bar_file **service_bar);

/** Close every host buffer, disable the translator, free the manager. */
void cleanup_hostmem(struct hostmem *hm);

/** True if the service BAR still shows the translator (the design may have changed). */
bool hostmem_present(const struct hostmem *hm);

/** Fill a HOST_MEM_INFO response. */
void hostmem_info(struct hostmem *hm, struct vrtd_resp_host_mem_info *resp);

/**
 * Open a host buffer of at least @p size bytes for @p client_id.
 *
 * @param fd_out    [out] Buffer fd to send to the client (vrtd keeps its own copy).
 * @return VRTD_RET_OK or a VRTD_RET_* error.
 */
uint16_t hostmem_buffer_open(struct hostmem *hm, uint64_t client_id, uint32_t flags, uint64_t size,
                             int *fd_out, uint64_t *card_addr_out, uint64_t *size_out);

/** Close the host buffer at @p card_addr owned by @p client_id. */
uint16_t hostmem_buffer_close(struct hostmem *hm, uint64_t client_id, uint64_t card_addr);

/** Close every host buffer of @p client_id (client disconnect). */
void hostmem_cleanup_client(struct hostmem *hm, uint64_t client_id);

#endif // VRTD_HOSTMEM_H
