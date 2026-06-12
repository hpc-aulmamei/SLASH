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
 * @file slash_qdma.c
 *
 * QDMA (Queue-based DMA) subsystem for the SLASH FPGA driver.
 *
 * This file implements the QDMA data-plane for SLASH, an AMD Alveo V80
 * partial-reconfiguration FPGA design.  It wraps the Xilinx libqdma
 * library (from submodules/qdma_drv/QDMA/linux-kernel/driver/libqdma/)
 * to provide queue-pair-based DMA transfers between host memory and the
 * FPGA fabric.
 *
 * The QDMA subsystem binds to PF1 (PCI device ID 0x50B5, or 0x50BD on
 * AVED/V80P designs), while the control device (slash_ctldev) binds to
 * PF2 (device ID 0x50B6).
 *
 * Queue pair lifecycle:
 *   add -> start -> I/O (via anon_inode fd) -> stop -> del
 *
 * Key design decisions:
 *   - **Poll mode** (no interrupts): avoids interrupt overhead for
 *     streaming workloads; the host polls HW-written completion status.
 *   - **Synchronous transfers**: qdma_request_submit() blocks until the
 *     DMA completes or times out (10 s default).
 *   - **XArray for qpair tracking**: provides dynamic ID allocation,
 *     built-in locking, and automatic index management for up to 256
 *     concurrent queue pairs.
 *   - **Reference counting**: kref on both the device and each qpair
 *     entry; the anon_inode fd holds a ref, preventing premature
 *     destruction while userspace still has the fd open.
 */

#include "slash_qdma.h"

#include "libqdma_export.h"

#include "slash.h"

#include <asm/cacheflush.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/miscdevice.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>
#include <linux/anon_inodes.h>

/*
 * Direction bitmask constants.
 *
 * These map 1:1 with the libqdma queue_type_t enum values (Q_H2C,
 * Q_C2H, Q_CMPT) but expressed as bit positions so they can be
 * OR'd together in a single dir_mask field.
 *
 * SLASH_QDMA_DIR_H2C  — Host-to-Card (write path)
 * SLASH_QDMA_DIR_C2H  — Card-to-Host (read path)
 * SLASH_QDMA_DIR_CMPT — Completion queue (status/metadata from card)
 */
#define SLASH_QDMA_DIR_H2C  BIT(0)
#define SLASH_QDMA_DIR_C2H  BIT(1)
#define SLASH_QDMA_DIR_CMPT BIT(2)
#define SLASH_QDMA_DIR_MASK (SLASH_QDMA_DIR_H2C | SLASH_QDMA_DIR_C2H | \
                             SLASH_QDMA_DIR_CMPT)

/*
 * Minimum user_size accepted by each QDMA ioctl. For input-bearing ioctls
 * this is the end-offset of the trailing input field — callers with a
 * smaller user_size would silently send zero-filled inputs after the
 * versioned copy-in, so the handler must refuse with -EINVAL before
 * acting on them. QDMA_IOCTL_INFO has no input fields beyond `size`, so
 * its minimum is the size field on its own — a caller passing size==0
 * has either forgotten to initialise the struct or claimed an incoherent
 * "my struct has zero bytes", both of which the kernel rejects.
 */
#define SLASH_QDMA_INFO_MIN_SIZE \
    offsetofend(struct slash_qdma_info, size)
#define SLASH_QDMA_QPAIR_ADD_MIN_SIZE \
    offsetofend(struct slash_qdma_qpair_add, cmpt_ring_sz)
#define SLASH_QDMA_QPAIR_OP_MIN_SIZE \
    offsetofend(struct slash_qdma_qpair_op, op)
#define SLASH_QDMA_QPAIR_GET_FD_MIN_SIZE \
    offsetofend(struct slash_qdma_qpair_fd_request, flags)
#define SLASH_QDMA_BUF_REGISTER_MIN_SIZE \
    offsetofend(struct slash_qdma_buf_register, length)
#define SLASH_QDMA_BUF_UNREGISTER_MIN_SIZE \
    offsetofend(struct slash_qdma_buf_unregister, buf_id)
#define SLASH_QDMA_TRANSFER_MIN_SIZE \
    offsetofend(struct slash_qdma_transfer, direction)

/*
 * CPM5 Host Profile indirect-context programming.
 *
 * The Host Profile context tells the CPM5 QDMA how to route AXI4-MM
 * traffic onto the Versal NoC.  It is programmed via the same indirect
 * context command interface libqdma uses for queue contexts, but with
 * the host-profile selector (0xA).  Register offsets and the command
 * word layout mirror eqdma_cpm5_reg.h:
 *
 *   IND_CTXT_DATA  base 0x804 (8 x u32 context words)
 *   IND_CTXT_MASK  base 0x824 (8 x u32 write masks)
 *   IND_CTXT_CMD   0x844      (busy[0], sel[4:1], op[6:5], qid[18:7])
 *
 * We program two profiles so the per-queue SW-context host_id selects
 * the NoC channel: Host ID 0 -> NoC Channel 0, Host ID 1 -> NoC Channel 1.
 */
#define SLASH_QDMA_HP_DATA_ADDR  0x804u
#define SLASH_QDMA_HP_MASK_ADDR  0x824u
#define SLASH_QDMA_HP_CMD_ADDR   0x844u
#define SLASH_QDMA_HP_CMD_BUSY   BIT(0)
#define SLASH_QDMA_HP_NUM_WORDS  8
#define SLASH_QDMA_HP_SEL        0xAu   /* QDMA_CTXT_SELC_HOST_PROFILE */
#define SLASH_QDMA_HP_OP_WR      0x1u   /* indirect context WR opcode */
#define SLASH_QDMA_HP_OP_RD      0x2u   /* indirect context RD opcode */
#define SLASH_QDMA_HP_SMID_BASE  0x100u /* bit 8 set; base AXI-MM master ID */
#define SLASH_QDMA_HP_POLL_US    1000   /* busy-wait budget in microseconds */

/*
 * The qpair fd data path accepts either a span of 4 KiB base pages or a span
 * of 2 MiB hugetlb pages.  Every scatter-gather entry within one request uses
 * the same granule, which keeps the DMA mapping semantics unambiguous; the two
 * granules are never mixed in a single request.  A whole transfer (of either
 * granule) is submitted to libqdma as a single multi-descriptor request, and
 * libqdma refills the descriptor ring as needed -- so the transfer size is not
 * bounded by the ring depth.
 */
#define SLASH_QDMA_HUGEPAGE_SIZE (2UL * 1024UL * 1024UL)

/*
 * qdma_huge_desc_size - Experimental descriptor granularity for hugetlb-backed
 * raw qpair transfers.
 *
 * The userspace raw-transfer path prefers 2 MiB hugetlb pages so the host page
 * size stays large and stable.  By default, each pinned 2 MiB page becomes one
 * SGL entry / QDMA descriptor.  Reducing this value keeps the same pinned
 * hugetlb page but emits several descriptors with increasing offsets inside
 * that page, allowing us to test whether descriptor pressure (rather than host
 * page size) is what makes dma-perf faster.
 *
 * Must be a page-aligned divisor of 2 MiB.  Examples:
 *   2097152 -> current behaviour (1 descriptor per huge page)
 *     65536 -> 32 descriptors per huge page
 *      4096 -> 512 descriptors per huge page
 */
static unsigned int qdma_huge_desc_size = SLASH_QDMA_HUGEPAGE_SIZE;

static int slash_qdma_huge_desc_size_set(const char *val,
                                         const struct kernel_param *kp)
{
    unsigned int parsed;
    int err;

    err = kstrtouint(val, 0, &parsed);
    if (err)
        return err;

    if (parsed < PAGE_SIZE ||
        parsed > SLASH_QDMA_HUGEPAGE_SIZE ||
        !IS_ALIGNED(parsed, PAGE_SIZE) ||
        (SLASH_QDMA_HUGEPAGE_SIZE % parsed) != 0)
        return -EINVAL;

    return param_set_uint(val, kp);
}

static const struct kernel_param_ops slash_qdma_huge_desc_size_ops = {
    .set = slash_qdma_huge_desc_size_set,
    .get = param_get_uint,
};

module_param_cb(qdma_huge_desc_size, &slash_qdma_huge_desc_size_ops,
                &qdma_huge_desc_size, 0644);
MODULE_PARM_DESC(qdma_huge_desc_size,
    "Descriptor size for 2 MiB hugetlb raw transfers; page-aligned divisor of 2 MiB (default 2097152)");

/**
 * SLASH_QDMA_QTYPE_COUNT - Number of queue types tracked per queue pair.
 *
 * Equals Q_CMPT + 1 (i.e., 3): one slot each for H2C, C2H, and CMPT.
 * Used to size the per-qpair qhndl[] array.
 */
#define SLASH_QDMA_QTYPE_COUNT (Q_CMPT + 1)

/**
 * SLASH_QDMA_MAX_QPAIRS - Maximum number of simultaneous queue pairs.
 *
 * This matches the conf.qsets_max value passed to qdma_device_open()
 * in slash_qdma_conf_options(), keeping the xarray ID space and the
 * HW queue-set limit in sync.
 */
#define SLASH_QDMA_MAX_QPAIRS 256

/*
 * Upper bound on the number of pages pinned per get_user_pages_fast() call when
 * mapping a multi-page base-page transfer.  Bounds the work done in a single
 * GUP call (and keeps the per-call page count within int range) while still
 * pinning large buffers in only a handful of iterations.
 */
#define SLASH_QDMA_GUP_BATCH 8192u

/**
 * SLASH_QDMA_QPAIR_ID_RANGE - XArray allocation range for qpair IDs.
 *
 * Constrains xa_alloc() to assign IDs in [0, 255].  The xarray handles
 * thread-safe allocation and lookup of queue pair entries within this
 * range.
 */
#define SLASH_QDMA_QPAIR_ID_RANGE XA_LIMIT(0, SLASH_QDMA_MAX_QPAIRS - 1)

/**
 * SLASH_QDMA_MAX_BUFS - Maximum number of registered DMA buffers per client.
 *
 * Each control-fd open instance gets its own buffer-id space; this bounds
 * the xarray allocation range used by SLASH_QDMA_IOCTL_BUF_REGISTER.
 */
#define SLASH_QDMA_MAX_BUFS 4096

/** XArray allocation range for registered buffer IDs ([0, 4095]). */
#define SLASH_QDMA_BUF_ID_RANGE XA_LIMIT(0, SLASH_QDMA_MAX_BUFS - 1)

/*
 * Debug logging infrastructure.
 *
 * When SLASH_QDMA_OP_DEBUG is non-zero (compile-time flag), every
 * libqdma call and state transition is logged via pr_info / dev_info.
 * In production builds the macros expand to nothing to avoid log spam.
 */
#ifndef SLASH_QDMA_OP_DEBUG
#define SLASH_QDMA_OP_DEBUG 0
#endif

#if SLASH_QDMA_OP_DEBUG
#define SLASH_QDMA_OP_LOG(fmt, ...) \
    pr_info("slash: qdma: " fmt, ##__VA_ARGS__)
#define SLASH_QDMA_OP_DEV_LOG(dev, fmt, ...) \
    dev_info((dev), "slash: qdma: " fmt, ##__VA_ARGS__)
#else
#define SLASH_QDMA_OP_LOG(fmt, ...) \
    do {                             \
    } while (0)
#define SLASH_QDMA_OP_DEV_LOG(dev, fmt, ...) \
    do {                                      \
    } while (0)
#endif

/*
 * Per-transfer timing instrumentation.
 *
 * When SLASH_QDMA_TIMING is non-zero (compile-time flag, e.g. built with
 * -DSLASH_QDMA_TIMING=1), slash_qdma_qpair_read_write() emits one dev_info
 * line per transfer breaking down the wall-clock cost of the kernel-side
 * phases:
 *
 *   - map:    pin user pages, validate page shape, build the SGL
 *             (slash_qdma_map_user_buf_to_sgl()).
 *   - submit: the whole libqdma qdma_request_submit() call, which covers
 *             SGL DMA-mapping (IOMMU), descriptor-ring fill, the PIDX
 *             doorbell, and the synchronous completion wait (HW transfer +
 *             poll-mode spin).  libqdma can be built with QDMA_TIMING=1 for
 *             a finer breakdown of this phase.
 *   - unmap:  unpin pages (mark dirty for C2H) and free the SGL.
 *
 * Timestamps use ktime_get() (CLOCK_MONOTONIC); the reads are cheap, but
 * the whole block compiles out entirely when the flag is 0.
 */
#ifndef SLASH_QDMA_TIMING
#define SLASH_QDMA_TIMING 0
#endif

/* Forward declaration; full definition follows. */
struct slash_qdma_dev;

/**
 * struct slash_qdma_qpair_entry - Per-queue-pair state.
 * @ref:        Reference count.  Starts at 1 (held by the xarray slot);
 *              an additional ref is taken when an anon_inode fd is handed
 *              to userspace, so the entry outlives the xarray removal if
 *              the fd is still open.
 * @qhndl:     Array of libqdma queue handles, one per queue type
 *              (Q_H2C, Q_C2H, Q_CMPT).  Entries that are not in use
 *              hold the sentinel QDMA_QUEUE_IDX_INVALID.
 * @dir_mask:   Bitmask of active directions (SLASH_QDMA_DIR_H2C, etc.).
 *              Updated as individual queues are added or removed.
 * @mode:       Queue operating mode (QDMA_Q_MODE_MM or QDMA_Q_MODE_ST).
 * @irq_mode:   Interrupt mode.  Currently always 0 (poll mode).
 * @irq_vector: MSI-X vector assignment.  Currently unused (poll mode).
 */
struct slash_qdma_qpair_entry {
    struct kref ref;
    unsigned long qhndl[SLASH_QDMA_QTYPE_COUNT];
    u32 dir_mask;
    enum qdma_q_mode mode;
    u32 irq_mode;
    u32 irq_vector;
};

/**
 * struct slash_qdma_dev - Per-PCI-device QDMA state.
 * @pdev:               The PCI device (PF1) this instance is bound to.
 * @qdma_handle:        Opaque handle returned by qdma_device_open();
 *                      passed to every subsequent libqdma call.
 * @misc:               Miscdevice registered under /dev/slash_qdma_ctlN.
 *                      Userspace opens this to issue queue management ioctls.
 * @ref:                Device-level reference count.  The miscdevice open
 *                      path and each anon_inode fd hold a ref; the device
 *                      structure is freed when the last ref drops.
 * @lock:               Serialises ioctl paths and protects @qpairs,
 *                      @hw_shutdown, and @have_qdma_handle.
 * @qpairs:             XArray mapping qpair IDs (u32) to
 *                      &struct slash_qdma_qpair_entry pointers.  Using an
 *                      xarray gives us O(1) lookup, thread-safe auto-ID
 *                      allocation, and safe concurrent iteration during
 *                      teardown.
 * @have_qdma_handle:   True once qdma_device_open() succeeds; false after
 *                      qdma_device_close().  Guards against use-after-close.
 * @is_misc_registered: True while the miscdevice is live.  Prevents double
 *                      deregistration on error paths.
 * @hw_shutdown:        Set to true during destroy to signal that the HW is
 *                      going away.  Any ioctl arriving after this flag is
 *                      set returns -ENODEV immediately.
 *
 * The three booleans (@have_qdma_handle, @is_misc_registered,
 * @hw_shutdown) track partially-constructed state during probe/remove
 * error paths; outside of create/destroy they should always reflect a
 * fully initialised device.
 */
struct slash_qdma_dev {
    struct pci_dev *pdev;
    unsigned long qdma_handle;

    struct miscdevice misc;
    struct kref ref;
    struct mutex lock;
    struct xarray qpairs;

    /*
     * Initialization booleans.
     * Assume these are always true outside of create/destroy.
     */
    bool have_qdma_handle;
    bool is_misc_registered;
    bool hw_shutdown;
};

/**
 * typedef slash_qdma_queue_cmd_fn - Function pointer for queue lifecycle ops.
 *
 * Matches the signature of qdma_queue_start(), qdma_queue_stop(), and
 * slash_qdma_queue_remove_safe(), allowing slash_qdma_ioctl_qpair_op_apply()
 * to iterate over all directions in a queue pair and apply the same
 * operation generically.
 */
typedef int (*slash_qdma_queue_cmd_fn)(unsigned long qdma_handle,
                                       unsigned long qhndl,
                                       char *errbuf,
                                       int errbuf_sz);

/* Forward declaration — defined below after its helper functions. */
static int slash_qdma_queue_remove_safe(unsigned long qdma_handle,
                                        unsigned long qhndl,
                                        char *errbuf,
                                        int errbuf_sz);

/* ─────────────────────────────────────────────────────────────────────
 * Direction / queue-type conversion helpers
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_dir_to_qtype() - Convert a direction bitmask bit to a queue type.
 * @dir_bit: Exactly one of SLASH_QDMA_DIR_H2C, _C2H, or _CMPT.
 *
 * Return: The corresponding libqdma queue_type_t value.
 *
 * Note: currently unused (hence __attribute__((unused))), but kept as
 * the inverse of slash_qdma_qtype_to_dir() for completeness.
 */
__attribute__((unused))
static enum queue_type_t slash_qdma_dir_to_qtype(u32 dir_bit)
{
    switch (dir_bit) {
    case SLASH_QDMA_DIR_H2C:
        return Q_H2C;
    case SLASH_QDMA_DIR_C2H:
        return Q_C2H;
    case SLASH_QDMA_DIR_CMPT:
        return Q_CMPT;
    default:
        return Q_H2C; /* should never reach */
    }
}

/**
 * slash_qdma_qtype_to_dir() - Convert a queue type to its direction bitmask bit.
 * @qtype: One of Q_H2C, Q_C2H, or Q_CMPT.
 *
 * Return: The corresponding SLASH_QDMA_DIR_* bitmask value, or 0 for
 *         an unrecognised queue type.
 */
static u32 slash_qdma_qtype_to_dir(enum queue_type_t qtype)
{
    switch (qtype) {
    case Q_H2C:
        return SLASH_QDMA_DIR_H2C;
    case Q_C2H:
        return SLASH_QDMA_DIR_C2H;
    case Q_CMPT:
        return SLASH_QDMA_DIR_CMPT;
    default:
        return 0;
    }
}

/**
 * slash_qdma_qhndl_is_valid() - Check if a queue handle is valid.
 * @qhndl: Queue handle from libqdma.
 *
 * Return: true if @qhndl is not the sentinel QDMA_QUEUE_IDX_INVALID,
 *         meaning the queue has been successfully added to the HW.
 */
static inline bool slash_qdma_qhndl_is_valid(unsigned long qhndl)
{
    return qhndl != QDMA_QUEUE_IDX_INVALID;
}

/* ─────────────────────────────────────────────────────────────────────
 * Queue removal with state-machine safety
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_queue_remove_safe() - Stop-then-remove a queue, handling any state.
 * @qdma_handle: Device handle from qdma_device_open().
 * @qhndl:       Queue handle to remove.
 * @errbuf:      Buffer for libqdma error messages.
 * @errbuf_sz:   Size of @errbuf.
 *
 * The QDMA HW queue state machine requires that an ONLINE queue be
 * stopped before it can be removed.  This helper queries the current
 * state and performs the correct transitions:
 *
 *   - Q_STATE_ONLINE   -> stop, then remove
 *   - Q_STATE_ENABLED  -> remove directly (already stopped)
 *   - Q_STATE_DISABLED -> no-op (already removed)
 *   - anything else    -> return -EINVAL
 *
 * This "check-before-stop" pattern prevents errors from trying to stop
 * an already-stopped queue or remove an already-removed one, which is
 * important during teardown where we may not know the current state.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_queue_remove_safe(unsigned long qdma_handle,
                                        unsigned long qhndl,
                                        char *errbuf,
                                        int errbuf_sz)
{
    struct qdma_q_state qstate = {0};
    int err;

    if (!errbuf || errbuf_sz <= 0)
        return -EINVAL;

    errbuf[0] = '\0';

    /* Query the current HW queue state */
    SLASH_QDMA_OP_LOG("qdma_get_queue_state start: handle=%lu qhndl=%lu\n",
                      qdma_handle, qhndl);
    err = qdma_get_queue_state(qdma_handle, qhndl, &qstate, errbuf, errbuf_sz);
    if (err) {
        SLASH_QDMA_OP_LOG("qdma_get_queue_state failed: qhndl=%lu err=%d (%s)\n",
                          qhndl, err, errbuf);
        return err;
    }
    SLASH_QDMA_OP_LOG("qdma_get_queue_state done: qhndl=%lu state=%u\n",
                      qhndl, qstate.qstate);

    switch (qstate.qstate) {
    case Q_STATE_ONLINE:
        /* Queue is active — must stop before removing. */
        SLASH_QDMA_OP_LOG("qdma_queue_stop start: qhndl=%lu\n", qhndl);
        err = qdma_queue_stop(qdma_handle, qhndl, errbuf, errbuf_sz);
        if (err) {
            SLASH_QDMA_OP_LOG("qdma_queue_stop failed: qhndl=%lu err=%d (%s)\n",
                              qhndl, err, errbuf);
            return err;
        }
        SLASH_QDMA_OP_LOG("qdma_queue_stop done: qhndl=%lu\n", qhndl);
        break;
    case Q_STATE_ENABLED:
        /* Queue is added but not started — can remove directly. */
        break;
    case Q_STATE_DISABLED:
        /* Queue is already removed. */
        SLASH_QDMA_OP_LOG("queue already disabled, skip remove: qhndl=%lu\n",
                          qhndl);
        return 0;
    default:
        snprintf(errbuf, errbuf_sz, "queue in unexpected state %u",
                 qstate.qstate);
        SLASH_QDMA_OP_LOG("qdma_get_queue_state unexpected state: qhndl=%lu state=%u\n",
                          qhndl, qstate.qstate);
        return -EINVAL;
    }

    /* State is now ENABLED — safe to remove. */
    SLASH_QDMA_OP_LOG("qdma_queue_remove start: qhndl=%lu\n", qhndl);
    err = qdma_queue_remove(qdma_handle, qhndl, errbuf, errbuf_sz);
    if (err) {
        SLASH_QDMA_OP_LOG("qdma_queue_remove failed: qhndl=%lu err=%d (%s)\n",
                          qhndl, err, errbuf);
        return err;
    }
    SLASH_QDMA_OP_LOG("qdma_queue_remove done: qhndl=%lu\n", qhndl);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * Queue pair xarray helpers (lookup, refcount, insert, remove)
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_qpair_lookup() - Find a qpair entry by ID.
 * @qdma_dev: QDMA device whose xarray to search.
 * @qid:      Queue pair ID.
 *
 * Return: Pointer to the entry, or NULL if @qid is not allocated.
 *
 * Note: the caller must hold @qdma_dev->lock or otherwise guarantee
 * that the entry will not be freed during use (e.g., by holding a ref).
 */
static inline struct slash_qdma_qpair_entry *
slash_qdma_qpair_lookup(struct slash_qdma_dev *qdma_dev, u32 qid)
{
    return xa_load(&qdma_dev->qpairs, qid);
}

/**
 * slash_qdma_qpair_entry_release() - kref release callback for qpair entries.
 * @ref: kref embedded in the slash_qdma_qpair_entry being released.
 *
 * Called when the last reference to a qpair entry is dropped.  Frees
 * the entry structure.  By this point, all associated HW queues must
 * already have been removed.
 */
static void slash_qdma_qpair_entry_release(struct kref *ref)
{
    struct slash_qdma_qpair_entry *entry =
        container_of(ref, struct slash_qdma_qpair_entry, ref);

    kfree(entry);
}

/**
 * slash_qdma_qpair_get() - Acquire a reference on a qpair entry.
 * @entry: The entry to reference.
 *
 * Used when handing out an anon_inode fd so the entry survives until
 * the fd is closed, even if the qpair is deleted from the xarray.
 */
static inline void slash_qdma_qpair_get(struct slash_qdma_qpair_entry *entry)
{
    kref_get(&entry->ref);
}

/**
 * slash_qdma_qpair_put() - Release a reference on a qpair entry.
 * @entry: The entry to dereference.
 *
 * When the last reference drops, the entry is freed via
 * slash_qdma_qpair_entry_release().
 */
static inline void slash_qdma_qpair_put(struct slash_qdma_qpair_entry *entry)
{
    kref_put(&entry->ref, slash_qdma_qpair_entry_release);
}

/**
 * slash_qdma_qpair_insert() - Allocate a new qpair ID and insert the entry.
 * @qdma_dev: QDMA device whose xarray receives the entry.
 * @entry:    The new entry to insert.  Its kref is initialised here.
 * @id:       [out] The auto-assigned queue pair ID.
 *
 * Uses xa_alloc() to atomically pick the lowest available ID in
 * [0, SLASH_QDMA_MAX_QPAIRS-1] and store @entry at that index.
 *
 * Return: 0 on success, -EBUSY if all 256 IDs are in use, or other
 *         negative errno.
 */
static inline int
slash_qdma_qpair_insert(struct slash_qdma_dev *qdma_dev, struct slash_qdma_qpair_entry *entry, u32 *id)
{
    kref_init(&entry->ref);
    return xa_alloc(&qdma_dev->qpairs, id, entry, SLASH_QDMA_QPAIR_ID_RANGE, GFP_KERNEL);
}

/**
 * slash_qdma_qpair_remove() - Erase a qpair from the xarray and drop its ref.
 * @qdma_dev: QDMA device.
 * @qid:      Queue pair ID to remove.
 *
 * After this call, the ID is available for reuse.  The entry itself is
 * only freed when all references (including any held by open fds) are
 * released.
 */
static inline void
slash_qdma_qpair_remove(struct slash_qdma_dev *qdma_dev, u32 qid)
{
    struct slash_qdma_qpair_entry *entry;

    entry = xa_erase(&qdma_dev->qpairs, qid);
    if (entry)
        slash_qdma_qpair_put(entry);
}

/* ─────────────────────────────────────────────────────────────────────
 * Anon-inode file context and I/O control block
 * ───────────────────────────────────────────────────────────────────── */

/**
 * struct slash_qdma_qpair_file_ctx - Private data for an anon_inode qpair fd.
 * @qdma_dev: Back-pointer to the owning QDMA device (ref held).
 * @entry:    The queue pair entry this fd operates on (ref held).
 * @qid:      Queue pair ID, cached for debug logging.
 *
 * Allocated in slash_qdma_ioctl_qpair_get_fd_w() and freed in
 * slash_qdma_qpair_release().  Both @qdma_dev and @entry have their
 * reference counts incremented when the ctx is created, and decremented
 * when the fd is closed.
 */
struct slash_qdma_qpair_file_ctx {
    struct slash_qdma_dev *qdma_dev;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_client *client;
    u32 qid;
};

/**
 * struct slash_qdma_io_cb - I/O control block for a single DMA transfer.
 * @buf:      User-space buffer address (source for H2C, destination for C2H).
 * @len:      Transfer length in bytes.
 * @pages_nr: Number of user pages pinned by get_user_pages_fast().
 * @sgl:      Scatter-gather list of qdma_sw_sg entries, one per pinned page.
 *            Allocated as a single contiguous block together with @pages.
 * @pages:    Array of struct page pointers for the pinned user pages.
 *            Points into the same allocation as @sgl (immediately after it).
 * @req:      The libqdma request structure submitted to qdma_request_submit().
 *
 * This is a stack-local structure (allocated in slash_qdma_qpair_read_write)
 * that bundles all per-transfer state.  The SGL and page array are heap-
 * allocated in slash_qdma_map_user_buf_to_sgl() and freed in
 * slash_qdma_iocb_release().
 */
struct slash_qdma_io_cb {
    void __user *buf;
    size_t len;
    unsigned int pages_nr;
    struct qdma_sw_sg *sgl;
    struct page **pages;
    struct qdma_request req;
};

/**
 * struct slash_qdma_buf - A registered (persistently pinned) host buffer.
 * @ref:        Reference count.  The owning client's xarray holds one ref;
 *              each in-flight transfer takes a temporary ref so an
 *              unregister cannot tear the buffer down under active DMA.
 * @qdma_dev:   Device whose DMA mappings back this buffer (non-owning; the
 *              owning client holds the device reference).
 * @buf_id:     Client-scoped handle returned to userspace.
 * @length:     Registered length in bytes.
 * @granule:    Bytes per SGL entry (PAGE_SIZE for base pages, or the
 *              hugepage descriptor size).  Uniform across all entries, so
 *              transfer slices can be computed by simple division.
 * @iocb:       Pinned pages and prebuilt scatter-gather list.  Each entry's
 *              dma_addr is filled in once at registration so transfers can
 *              submit with req->dma_mapped = 1.
 *
 * Registered buffers amortise the per-transfer cost of pinning pages,
 * building the SGL, and programming the IOMMU: that work happens once at
 * registration, and every transfer reuses the cached, pre-DMA-mapped SGL.
 */
struct slash_qdma_buf {
    struct kref ref;
    struct slash_qdma_dev *qdma_dev;
    u32 buf_id;
    u64 length;
    u64 granule;
    struct slash_qdma_io_cb iocb;
};

/**
 * struct slash_qdma_client - Per-open state for the QDMA control device.
 * @ref:      Reference count.  The control fd holds the initial ref; each
 *            qpair I/O fd handed out via QPAIR_GET_FD takes another so that
 *            handle-based transfers can resolve buffer IDs even if the
 *            control fd is closed first.
 * @qdma_dev: Owning QDMA device (holds a device reference).
 * @buffers:  XArray mapping buf_id -> &struct slash_qdma_buf.  Buffers are
 *            owned by this client and auto-freed when the control fd closes.
 *
 * Replaces the bare device pointer previously stored in the control fd's
 * file->private_data.  Tying registered buffers to this per-open context
 * makes cleanup automatic: if userspace exits or is killed without
 * unregistering, the control fd release path drops every buffer.
 */
struct slash_qdma_client {
    struct kref ref;
    struct slash_qdma_dev *qdma_dev;
    struct xarray buffers;
};

/* ─────────────────────────────────────────────────────────────────────
 * Forward declarations
 * ───────────────────────────────────────────────────────────────────── */

static int slash_qdma_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void slash_qdma_remove(struct pci_dev *pdev);
static int slash_qdma_create_qdma_device(struct pci_dev *pdev, struct slash_qdma_dev **pdevice);
static void slash_qdma_destroy_qdma_device(struct slash_qdma_dev *device);
static void slash_qdma_dev_release(struct kref *ref);
static void slash_qdma_conf_options(struct qdma_dev_conf *conf, struct pci_dev *pdev);
static int slash_qdma_ioctl_info_w(struct miscdevice *misc,
                                   struct slash_qdma_dev *qdma_dev,
                                   void __user *uarg);
static int slash_qdma_ioctl_qpair_add_w(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         void __user *uarg);
static int slash_qdma_ioctl_qpair_add(struct miscdevice *misc,
                                      struct slash_qdma_dev *qdma_dev,
                                      struct slash_qdma_qpair_add *req);
static int slash_qdma_ioctl_qpair_add_q(struct miscdevice *misc,
                                        struct slash_qdma_dev *qdma_dev,
                                        struct slash_qdma_qpair_add *req,
                                        struct slash_qdma_qpair_entry *entry,
                                        enum queue_type_t qtype);
static void slash_qdma_ioctl_qpair_rm_q(struct miscdevice *misc,
                                        struct slash_qdma_dev *qdma_dev,
                                        struct slash_qdma_qpair_entry *entry,
                                        enum queue_type_t qtype);
static int slash_qdma_ioctl_qpair_op_w(struct miscdevice *misc,
                                       struct slash_qdma_dev *qdma_dev,
                                       void __user *uarg);
static int slash_qdma_ioctl_qpair_op(struct miscdevice *misc,
                                     struct slash_qdma_dev *qdma_dev,
                                     struct slash_qdma_qpair_op *req);
static int slash_qdma_ioctl_qpair_op_apply(struct slash_qdma_dev *qdma_dev,
                                           struct slash_qdma_qpair_entry *entry,
                                           struct slash_qdma_qpair_op *req,
                                           slash_qdma_queue_cmd_fn fn,
                                           const char *op_name,
                                           bool stop_on_err);
static int slash_qdma_ioctl_qpair_get_fd_w(struct miscdevice *misc,
                                           struct slash_qdma_client *client,
                                           void __user *uarg);
static int slash_qdma_ioctl_buf_register_w(struct miscdevice *misc,
                                           struct slash_qdma_client *client,
                                           void __user *uarg);
static int slash_qdma_ioctl_buf_unregister_w(struct miscdevice *misc,
                                             struct slash_qdma_client *client,
                                             void __user *uarg);
static void slash_qdma_buf_release(struct kref *ref);
static void slash_qdma_buf_put(struct slash_qdma_buf *buf);
static void slash_qdma_client_release(struct kref *ref);
static long slash_qdma_qpair_transfer(struct file *file, void __user *uarg);

static ssize_t slash_qdma_qpair_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos);
static ssize_t slash_qdma_qpair_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos);
static int slash_qdma_qpair_release(struct inode *inode, struct file *file);
static long slash_qdma_qpair_ioctl(struct file *file,
                                   unsigned int cmd, unsigned long arg);

/**
 * slash_qdma_qpair_fops - File operations for per-qpair anon_inode fds.
 *
 * read()  performs a C2H (card-to-host) DMA transfer.
 * write() performs an H2C (host-to-card) DMA transfer.
 * llseek  uses default_llseek so that pread/pwrite can set the
 *         device-side address via the file position.
 * ioctl   is a stub that returns -ENOTTY (no per-fd ioctls defined yet).
 * release drops the refs on the qpair entry and device.
 */
static const struct file_operations slash_qdma_qpair_fops = {
    .owner          = THIS_MODULE,
    .read           = slash_qdma_qpair_read,
    .write          = slash_qdma_qpair_write,
    .unlocked_ioctl = slash_qdma_qpair_ioctl,
    .release        = slash_qdma_qpair_release,
    .llseek         = default_llseek,
};


static int slash_qdma_fop_open(struct inode *inode, struct file *file);
static int slash_qdma_fop_release(struct inode *inode, struct file *file);
static long slash_qdma_fop_ioctl(struct file *file, unsigned int op, unsigned long arg);
static void slash_qdma_ioctl_info(struct miscdevice *misc, struct slash_qdma_dev *qdma_dev, struct slash_qdma_info *qdma_info);



/**
 * slash_qdma_ids - PCI device ID table for the QDMA PF.
 *
 * Matches PF1 QDMA functions on AMD/Xilinx V80 cards, including the
 * AVED/V80P device ID.
 */
static const struct pci_device_id slash_qdma_ids[] = {
    {PCI_DEVICE(SLASH_QDMA_PCI_VENDOR_ID, SLASH_QDMA_PCI_DEVICE_ID)},
    {PCI_DEVICE(SLASH_QDMA_PCI_VENDOR_ID, SLASH_AVED_QDMA_PCI_DEVICE_ID)},
    {0,}
};
MODULE_DEVICE_TABLE(pci, slash_qdma_ids);

/**
 * slash_qdma_driver - PCI driver structure for the QDMA subsystem.
 *
 * Registered in slash_qdma_init(); triggers slash_qdma_probe() for each
 * matching PF1 device discovered during PCI enumeration.
 */
static struct pci_driver slash_qdma_driver = {
    .name = SLASH_QDMA_DRV_NAME,
    .id_table = slash_qdma_ids,
    .probe = slash_qdma_probe,
    .remove = slash_qdma_remove,
};

/**
 * slash_qdma_fops - File operations for the QDMA control miscdevice.
 *
 * The miscdevice (/dev/slash_qdma_ctlN) is the management interface:
 * userspace opens it and issues ioctls to add/start/stop/delete queue
 * pairs and to obtain per-qpair I/O fds.
 */
static struct file_operations slash_qdma_fops = {
    .owner          = THIS_MODULE,
    .open           = slash_qdma_fop_open,
    .release        = slash_qdma_fop_release,
    .unlocked_ioctl = slash_qdma_fop_ioctl,
};

/* ─────────────────────────────────────────────────────────────────────
 * BDF-to-device-number map (stable /dev/slash_qdma_ctlN across hotplug)
 * ───────────────────────────────────────────────────────────────────── */

/**
 * struct slash_qdma_id_entry - Stable BDF-to-number mapping entry.
 * @node:    Intrusive list linkage for @slash_qdma_id_map.
 * @bdf:     Full PCI BDF string including function (e.g. "0000:61:00.1").
 * @number:  The /dev/slash_qdma_ctl<N> suffix permanently assigned to this BDF.
 * @in_use:  True while the device is bound to the driver.  Cleared on remove,
 *           set on probe.  A probe that finds @in_use already true indicates
 *           the kernel handed us a device that was never properly unbound —
 *           this should never happen under normal operation.
 *
 * Entries are allocated in probe and intentionally never freed.  They survive
 * hotplug remove+rescan cycles so that a device always gets back the same N.
 */
struct slash_qdma_id_entry {
    struct list_head node;
    char bdf[32]; /* "DDDD:BB:SS.F\0" fits comfortably in 32 bytes */
    int  number;
    bool in_use;
};

/** Persistent BDF-to-number map; entries live for the module's lifetime. */
static LIST_HEAD(slash_qdma_id_map);
/** Serialises all accesses to @slash_qdma_id_map and @in_use fields. */
static DEFINE_MUTEX(slash_qdma_id_map_lock);
/** Source of new numbers; only incremented when a BDF is seen for the first time. */
static atomic_t slash_qdma_devcount = ATOMIC_INIT(0);

/**
 * slash_qdma_id_get() - Look up or allocate a stable number for a BDF.
 * @bdf: Full PCI BDF string (e.g. "0000:61:00.1") from pci_name().
 *
 * Called from probe.  Returns the number permanently associated with @bdf,
 * allocating a new one if this BDF is seen for the first time.  Also marks
 * the entry as in_use = true.
 *
 * If an existing entry is found with in_use already set, the device was
 * never properly unbound before probe was called again — this indicates a
 * kernel PCI driver bug.  The function logs a loud error and returns
 * -EBUSY so that probe aborts without touching the device.
 *
 * Return: non-negative stable device number on success, negative errno on
 *         failure (-ENOMEM if allocation fails, -EBUSY if already in use).
 */
static int slash_qdma_id_get(const char *bdf)
{
    struct slash_qdma_id_entry *entry;
    int number;

    mutex_lock(&slash_qdma_id_map_lock);

    list_for_each_entry(entry, &slash_qdma_id_map, node) {
        if (strcmp(entry->bdf, bdf) != 0)
            continue;

        if (entry->in_use) {
            pr_err("slash_qdma: BUG: probe called for %s but entry is already in_use "
                   "(number=%d); refusing to bind\n", bdf, entry->number);
            mutex_unlock(&slash_qdma_id_map_lock);
            return -EBUSY;
        }

        entry->in_use = true;
        number = entry->number;
        mutex_unlock(&slash_qdma_id_map_lock);
        pr_info("slash_qdma: reusing number %d for %s\n", number, bdf);
        return number;
    }

    /* First time we've seen this BDF — allocate a fresh entry. */
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        mutex_unlock(&slash_qdma_id_map_lock);
        return -ENOMEM;
    }

    strscpy(entry->bdf, bdf, sizeof(entry->bdf));
    entry->number = atomic_inc_return(&slash_qdma_devcount) - 1;
    entry->in_use = true;
    list_add_tail(&entry->node, &slash_qdma_id_map);

    number = entry->number;
    mutex_unlock(&slash_qdma_id_map_lock);

    pr_info("slash_qdma: assigned number %d to %s\n", number, bdf);
    return number;
}

/**
 * slash_qdma_id_release() - Mark a BDF's entry as no longer in use.
 * @bdf: Full PCI BDF string passed to the matching slash_qdma_id_get() call.
 *
 * Called when the misc device is deregistered (remove path, or probe error
 * unwind after misc_register succeeds).  Clears in_use so that the next probe
 * for the same BDF can reuse the stored number.  The entry itself is not freed.
 *
 * If no entry exists for @bdf (should never happen after a successful probe),
 * the call is a no-op and a warning is logged.
 */
static void slash_qdma_id_release(const char *bdf)
{
    struct slash_qdma_id_entry *entry;

    mutex_lock(&slash_qdma_id_map_lock);

    list_for_each_entry(entry, &slash_qdma_id_map, node) {
        if (strcmp(entry->bdf, bdf) != 0)
            continue;

        entry->in_use = false;
        mutex_unlock(&slash_qdma_id_map_lock);
        pr_info("slash_qdma: released number %d for %s\n", entry->number, bdf);
        return;
    }

    /* Should be unreachable: release without a prior successful id_get. */
    pr_warn("slash_qdma: WARNING: release called for %s but no entry found\n", bdf);
    mutex_unlock(&slash_qdma_id_map_lock);
}

/* ─────────────────────────────────────────────────────────────────────
 * Module init / exit
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_init() - Initialise the QDMA subsystem.
 * @num_threads: Worker thread count for libqdma's internal processing.
 * @debugfs:     Optional debugfs mount path, or NULL to disable.
 *
 * Called from the top-level module init.  Initialises the libqdma
 * library first (which sets up internal data structures and worker
 * threads), then registers the PCI driver so that slash_qdma_probe()
 * fires for each PF1 device.
 *
 * Return: 0 on success, negative errno on failure.
 */
int __init slash_qdma_init(unsigned int num_threads, char *debugfs)
{
    int err;

    SLASH_QDMA_OP_LOG("init start: num_threads=%u debugfs=%s\n",
                      num_threads, debugfs ? debugfs : "(null)");

    err = libqdma_init(num_threads, debugfs);
    if (err) {
        SLASH_QDMA_OP_LOG("libqdma_init failed: err=%d\n", err);
        pr_err("slash: libqdma_init failed: %d\n", err);
        return err;
    }
    SLASH_QDMA_OP_LOG("libqdma_init done\n");

    err = pci_register_driver(&slash_qdma_driver);
    if (err) {
        SLASH_QDMA_OP_LOG("pci_register_driver failed: err=%d\n", err);
        pr_err("slash: register qdma driver failed: %d\n", err);
        goto err_exit_libqdma;
    }
    SLASH_QDMA_OP_LOG("pci_register_driver done\n");

    return 0;

err_exit_libqdma:
    SLASH_QDMA_OP_LOG("libqdma_exit start (init rollback)\n");
    libqdma_exit();
    SLASH_QDMA_OP_LOG("libqdma_exit done (init rollback)\n");

    return err;
}

/**
 * slash_qdma_exit() - Tear down the QDMA subsystem.
 *
 * Called from the top-level module exit.  Unregisters the PCI driver
 * (which triggers slash_qdma_remove() for each probed device) and then
 * shuts down the libqdma library.
 */
void slash_qdma_exit(void)
{
    SLASH_QDMA_OP_LOG("exit start\n");

    pci_unregister_driver(&slash_qdma_driver);
    SLASH_QDMA_OP_LOG("pci_unregister_driver done\n");

    libqdma_exit();
    SLASH_QDMA_OP_LOG("libqdma_exit done\n");
}

/* ─────────────────────────────────────────────────────────────────────
 * CPM5 Host Profile context programming
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_hp_set_field() - Set a bit field in the host profile context.
 * @words: Array of SLASH_QDMA_HP_NUM_WORDS u32s holding the 256-bit context
 *         (word i covers bits [32*i+31 : 32*i]).
 * @hi:    Most-significant bit index of the field (inclusive).
 * @lo:    Least-significant bit index of the field (inclusive).
 * @val:   Value to place in [hi:lo]; bits outside the field width are masked.
 *
 * Handles fields that straddle a 32-bit word boundary (e.g. the C2H
 * AXI4-MM steering field at bits [97:94], which spans words 2 and 3).
 */
static void slash_qdma_hp_set_field(u32 *words, unsigned int hi,
                                    unsigned int lo, u32 val)
{
    unsigned int width = hi - lo + 1;
    u32 fmask = (width >= 32) ? ~0u : ((1u << width) - 1u);
    unsigned int word = lo >> 5;
    unsigned int off = lo & 31;
    u64 wmask = (u64)fmask << off;
    u64 wval = (u64)(val & fmask) << off;

    words[word] = (words[word] & ~(u32)(wmask & 0xFFFFFFFFu)) |
                  (u32)(wval & 0xFFFFFFFFu);

    if ((off + width) > 32 && (word + 1) < SLASH_QDMA_HP_NUM_WORDS)
        words[word + 1] = (words[word + 1] & ~(u32)(wmask >> 32)) |
                          (u32)(wval >> 32);
}

/**
 * slash_qdma_hp_wait_ready() - Poll the indirect-context BUSY bit.
 * @device:  QDMA device (provides the libqdma handle for register access).
 * @val_out: If non-NULL, receives the last QDMA_IND_CTXT_CMD value read.
 *
 * Spins (up to SLASH_QDMA_HP_POLL_US microseconds) until the indirect
 * context command BUSY bit clears.  Logging is left to the caller so the
 * write path can treat a timeout as fatal while the readback path can treat
 * it as a warning.
 *
 * Return: 0 once not busy, -ETIMEDOUT on timeout, or a negative errno from
 *         the register read.
 */
static int slash_qdma_hp_wait_ready(struct slash_qdma_dev *device, u32 *val_out)
{
    unsigned int waited_us = 0;
    u32 val = 0;
    int err;

    do {
        err = qdma_device_read_config_register(device->qdma_handle,
                SLASH_QDMA_HP_CMD_ADDR, &val);
        if (err)
            return err;
        if (!(val & SLASH_QDMA_HP_CMD_BUSY)) {
            if (val_out)
                *val_out = val;
            return 0;
        }
        udelay(1);
    } while (++waited_us < SLASH_QDMA_HP_POLL_US);

    if (val_out)
        *val_out = val;
    return -ETIMEDOUT;
}

/**
 * slash_qdma_hp_get_field() - Read a bit field from the host profile context.
 * @words: Array of SLASH_QDMA_HP_NUM_WORDS u32s holding the 256-bit context
 *         (word i covers bits [32*i+31 : 32*i]).
 * @hi:    Most-significant bit index of the field (inclusive).
 * @lo:    Least-significant bit index of the field (inclusive).
 *
 * Inverse of slash_qdma_hp_set_field(); handles fields that straddle a
 * 32-bit word boundary (e.g. the C2H AXI4-MM steering field at bits
 * [97:94], which spans words 2 and 3).
 *
 * Return: the value held in [hi:lo].
 */
static u32 slash_qdma_hp_get_field(const u32 *words, unsigned int hi,
                                   unsigned int lo)
{
    unsigned int width = hi - lo + 1;
    u32 fmask = (width >= 32) ? ~0u : ((1u << width) - 1u);
    unsigned int word = lo >> 5;
    unsigned int off = lo & 31;
    u64 two = (u64)words[word];

    if ((word + 1) < SLASH_QDMA_HP_NUM_WORDS)
        two |= (u64)words[word + 1] << 32;

    return (u32)((two >> off) & fmask);
}

/**
 * slash_qdma_read_host_profile() - Read one CPM5 Host Profile entry back.
 * @device:  QDMA device (provides the libqdma handle for register access).
 * @host_id: Host Profile index to read.
 * @out:     Array of SLASH_QDMA_HP_NUM_WORDS u32s that receives the 256-bit
 *           context.
 *
 * Issues an indirect-context RD command for the host-profile selector,
 * waits for the controller to complete it, and copies the IND_CTXT_DATA
 * words back.  Used to verify a preceding write.
 *
 * Return: 0 on success, negative errno on register-access error or
 *         -ETIMEDOUT if the BUSY bit never clears.
 */
static int slash_qdma_read_host_profile(struct slash_qdma_dev *device,
                                        u32 host_id, u32 *out)
{
    u32 cmd = (host_id << 7) | (SLASH_QDMA_HP_OP_RD << 5) |
              (SLASH_QDMA_HP_SEL << 1);
    int err;
    int i;

    err = qdma_device_write_config_register(device->qdma_handle,
            SLASH_QDMA_HP_CMD_ADDR, cmd);
    if (err)
        return err;

    err = slash_qdma_hp_wait_ready(device, NULL);
    if (err)
        return err;

    for (i = 0; i < SLASH_QDMA_HP_NUM_WORDS; i++) {
        err = qdma_device_read_config_register(device->qdma_handle,
                SLASH_QDMA_HP_DATA_ADDR + (i * sizeof(u32)), &out[i]);
        if (err)
            return err;
    }

    return 0;
}

/**
 * slash_qdma_write_host_profile() - Program and verify one CPM5 Host Profile.
 * @device:  QDMA device (provides the libqdma handle for register access).
 * @host_id: Host Profile index to program (also the AXI4-MM steering value,
 *           i.e. the target NoC channel).
 *
 * Builds the 256-bit host profile context with the SMID and H2C/C2H
 * AXI4-MM steering fields, writes it through the indirect-context
 * registers via the libqdma-exported config register accessors, and
 * polls the command BUSY bit until the controller completes the write.
 *
 * Once the write completes it reads the profile back and verifies the
 * programmed fields (SMID and the two steering fields); a readback error
 * or field mismatch is logged but is non-fatal (the profile is still
 * considered applied).
 *
 * Only the SMID and the two steering fields are non-zero; the AXI
 * prot/cache attributes are left at 0.
 *
 * Return: 0 on success, negative errno on register-access error or
 *         -ETIMEDOUT if the BUSY bit never clears.
 */
static int slash_qdma_write_host_profile(struct slash_qdma_dev *device,
                                         u32 host_id)
{
    u32 data[SLASH_QDMA_HP_NUM_WORDS] = {0};
    u32 smid = SLASH_QDMA_HP_SMID_BASE + host_id;
    u32 cmd;
    u32 val = 0;
    int err;
    int i;

    /* SMID [201:192]; H2C steering [181:178]; C2H steering [97:94]. */
    slash_qdma_hp_set_field(data, 201, 192, smid);
    slash_qdma_hp_set_field(data, 181, 178, host_id);
    slash_qdma_hp_set_field(data, 97, 94, host_id);

    /* Context data words. */
    for (i = 0; i < SLASH_QDMA_HP_NUM_WORDS; i++) {
        err = qdma_device_write_config_register(device->qdma_handle,
                SLASH_QDMA_HP_DATA_ADDR + (i * sizeof(u32)), data[i]);
        if (err)
            goto err_reg;
    }

    /* Context masks: write every bit. */
    for (i = 0; i < SLASH_QDMA_HP_NUM_WORDS; i++) {
        err = qdma_device_write_config_register(device->qdma_handle,
                SLASH_QDMA_HP_MASK_ADDR + (i * sizeof(u32)), 0xFFFFFFFFu);
        if (err)
            goto err_reg;
    }

    /* Command: qid=host_id, op=WR, sel=HOST_PROFILE (0x34 for id 0, 0xB4 for id 1). */
    cmd = (host_id << 7) | (SLASH_QDMA_HP_OP_WR << 5) | (SLASH_QDMA_HP_SEL << 1);
    err = qdma_device_write_config_register(device->qdma_handle,
            SLASH_QDMA_HP_CMD_ADDR, cmd);
    if (err)
        goto err_reg;

    /* Wait for the controller to consume the command. */
    err = slash_qdma_hp_wait_ready(device, &val);
    if (err == -ETIMEDOUT) {
        dev_err(&device->pdev->dev,
                "qdma: host profile %u programming timed out (cmd=0x%x)\n",
                host_id, val);
        return -ETIMEDOUT;
    }
    if (err)
        goto err_reg;

    /*
     * Read the profile back and verify the programmed fields.  A readback
     * error or field mismatch is non-fatal: the write itself completed, so
     * the profile is still considered applied.
     */
    {
        u32 rb[SLASH_QDMA_HP_NUM_WORDS] = {0};
        int rerr = slash_qdma_read_host_profile(device, host_id, rb);

        if (rerr) {
            dev_warn(&device->pdev->dev,
                     "slash: qdma: host profile %u applied (cmd=0x%02x) but readback failed: %d\n",
                     host_id, cmd, rerr);
        } else {
            u32 smid_rb = slash_qdma_hp_get_field(rb, 201, 192);
            u32 h2c_rb = slash_qdma_hp_get_field(rb, 181, 178);
            u32 c2h_rb = slash_qdma_hp_get_field(rb, 97, 94);

            if (smid_rb == smid && h2c_rb == host_id && c2h_rb == host_id) {
                dev_info(&device->pdev->dev,
                         "slash: qdma: host profile %u applied and readback verified: H2C/C2H AXI-MM steering=%u (NoC channel %u), smid=0x%03x (cmd=0x%02x)\n",
                         host_id, host_id, host_id, smid, cmd);
            } else {
                dev_err(&device->pdev->dev,
                        "slash: qdma: host profile %u readback MISMATCH: smid exp=0x%03x got=0x%03x, h2c exp=%u got=%u, c2h exp=%u got=%u\n",
                        host_id, smid, smid_rb, host_id, h2c_rb,
                        host_id, c2h_rb);
            }
        }
    }
    return 0;

err_reg:
    dev_err(&device->pdev->dev,
            "qdma: host profile %u register access failed: %d\n",
            host_id, err);
    return err;
}

/**
 * slash_qdma_program_host_profiles() - Program the CPM5 Host Profiles.
 * @device: QDMA device.
 *
 * Programs Host Profile 0 (steer to NoC Channel 0) and Host Profile 1
 * (steer to NoC Channel 1).  Must run after qdma_device_open() (which
 * clears all contexts) and before any queue context is programmed, per
 * the CPM5 requirement that the host profile exist before AXI4-MM
 * queues are set up.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_program_host_profiles(struct slash_qdma_dev *device)
{
    u32 host_id;
    int err;

    dev_info(&device->pdev->dev,
             "slash: qdma: programming CPM5 host profiles (host_id 0 -> NoC channel 0, host_id 1 -> NoC channel 1)\n");

    for (host_id = 0; host_id <= 1; host_id++) {
        err = slash_qdma_write_host_profile(device, host_id);
        if (err)
            return err;
    }

    dev_info(&device->pdev->dev,
             "slash: qdma: CPM5 host profiles programmed\n");

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * PCI probe / remove
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_probe() - PCI probe callback for QDMA devices.
 * @pdev: The PCI device being probed.
 * @id:   Matching entry from slash_qdma_ids[].
 *
 * Verifies that the device is PF1 (the QDMA IP is only on PF1; PF2 is
 * the control function handled by slash_ctldev).  Then:
 *   1. Allocates and initialises a slash_qdma_dev structure.
 *   2. Configures and opens the libqdma device via qdma_device_open().
 *   3. Registers the management miscdevice (/dev/slash_qdma_ctlN).
 *
 * On any failure, the partially-constructed device is torn down and
 * the probe returns the error.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int err;
    struct qdma_dev_conf conf;
    struct slash_qdma_dev *device = NULL;

    memset(&conf, 0, sizeof(conf));

    dev_info(&pdev->dev, "slash: qdma: probe start for %s\n", pci_name(pdev));
    SLASH_QDMA_OP_DEV_LOG(&pdev->dev,
                          "probe details: vendor=0x%04x device=0x%04x fn=%u\n",
                          pdev->vendor, pdev->device, PCI_FUNC(pdev->devfn));

    /* Reject anything that is not PF1 — the QDMA IP lives only on PF1. */
    if (PCI_FUNC(pdev->devfn) != SLASH_QDMA_PF) {
        dev_err(&pdev->dev, "slash: expected PF %u, got %u\n", SLASH_QDMA_PF, PCI_FUNC(pdev->devfn));
        return -EINVAL;
    }

    /* Allocate and initialise the per-device structure. */
    err = slash_qdma_create_qdma_device(pdev, &device);
    if (err) {
        goto err_free;
    }

    /* Configure and open the libqdma device. */
    slash_qdma_conf_options(&conf, pdev);
    SLASH_QDMA_OP_DEV_LOG(&pdev->dev,
                          "qdma_device_open start: name=%s qsets_max=%d qsets_base=%d\n",
                          SLASH_NAME, conf.qsets_max, conf.qsets_base);
    err = qdma_device_open(SLASH_NAME, &conf, &device->qdma_handle);
    if (err) {
        SLASH_QDMA_OP_DEV_LOG(&pdev->dev, "qdma_device_open failed: err=%d\n",
                              err);
        dev_err(&pdev->dev, "slash: qdma: could not open qdma device %d", err);
        goto err_free;
    }
    SLASH_QDMA_OP_DEV_LOG(&pdev->dev,
                          "qdma_device_open done: handle=%lu\n",
                          device->qdma_handle);
    device->have_qdma_handle = true;

    /*
     * Program the CPM5 Host Profiles before exposing the miscdevice, so
     * they exist before userspace can add any queue.  Host ID 0 steers
     * AXI4-MM traffic to NoC Channel 0 and Host ID 1 to NoC Channel 1;
     * the per-queue SW-context host_id (mirrored from mm_channel = qid & 1)
     * selects between them.
     */
    err = slash_qdma_program_host_profiles(device);
    if (err) {
        dev_err(&pdev->dev,
                "slash: qdma: could not program host profiles: %d", err);
        goto err_free;
    }

    /* Register the management miscdevice so userspace can issue ioctls. */
    err = misc_register(&device->misc);
    if (err) {
        dev_err(&pdev->dev, "slash: qdma: could not register misc device: %d", err);
        /*
         * is_misc_registered is still false here, so slash_qdma_destroy_qdma_device
         * will not call misc_deregister or id_release.  Release the id explicitly.
         */
        slash_qdma_id_release(pci_name(pdev));
        goto err_free;
    }
    device->is_misc_registered = true;

    return 0;

err_free:
    if (device) {
        slash_qdma_destroy_qdma_device(device);
        kref_put(&device->ref, slash_qdma_dev_release);
    }

    return err;
}

/**
 * slash_qdma_remove() - PCI remove callback for QDMA devices.
 * @pdev: The PCI device being removed.
 *
 * Tears down all HW queues, closes the libqdma device, deregisters the
 * miscdevice, and drops the device reference.
 */
static void slash_qdma_remove(struct pci_dev *pdev)
{
    struct slash_qdma_dev *device = pci_get_drvdata(pdev);

    if (!device)
        return;

    slash_qdma_destroy_qdma_device(device);
    kref_put(&device->ref, slash_qdma_dev_release);
}

/* ─────────────────────────────────────────────────────────────────────
 * Device allocation and teardown
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_create_qdma_device() - Allocate and initialise a QDMA device.
 * @pdev:    PCI device to bind to.
 * @pdevice: [out] Receives a pointer to the new device on success.
 *
 * Allocates the slash_qdma_dev, initialises its mutex, xarray, kref,
 * and miscdevice fields, and stores it in the PCI drvdata.  A static
 * atomic counter provides unique /dev node numbering across devices.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_create_qdma_device(struct pci_dev *pdev, struct slash_qdma_dev **pdevice)
{
    int err;
    struct slash_qdma_dev *device;
    int id;

    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        return -ENOMEM;
    }
    device->pdev = pdev;
    kref_init(&device->ref);
    mutex_init(&device->lock);
    xa_init_flags(&device->qpairs, XA_FLAGS_ALLOC);
    device->hw_shutdown = false;
    pci_set_drvdata(pdev, device);

    { /* Miscdevice setup */
        device->misc.minor = MISC_DYNAMIC_MINOR;
        device->misc.fops = &slash_qdma_fops;
        device->misc.parent = &pdev->dev;
        device->misc.mode = SLASH_CTLDEV_QDMA_MODE;

        /* Name visible in /sys/class/misc, includes PCI BDF for uniqueness. */
        device->misc.name = kasprintf(GFP_KERNEL, SLASH_QDMA_CTLDEV_NAME_FMT, pci_name(device->pdev));
        if (!device->misc.name) {
            dev_err(&device->pdev->dev, "qdma: kasprintf(name) failed\n");
            err = -ENOMEM;
            goto err_free;
        }

        /* /dev node name: stable numeric index from BDF-to-number map. */
        id = slash_qdma_id_get(pci_name(device->pdev));
        if (id < 0) {
            dev_err(&device->pdev->dev, "qdma: id_get failed: %d\n", id);
            err = id;
            goto err_free_name;
        }

        device->misc.nodename = kasprintf(GFP_KERNEL, SLASH_QDMA_CTLDEV_NODENAME_FMT, id);
        if (!device->misc.nodename) {
            dev_err(&device->pdev->dev, "qdma: kasprintf(nodename) failed\n");
            err = -ENOMEM;
            goto err_release_id;
        }
    }

    *pdevice = device;
    return 0;

err_release_id:
    slash_qdma_id_release(pci_name(device->pdev));

err_free_name:
    kfree(device->misc.name);
    device->misc.name = NULL;

err_free:
    slash_qdma_destroy_qdma_device(device);
    *pdevice = NULL;

    return err;
}

/**
 * slash_qdma_destroy_qdma_device() - Tear down a QDMA device.
 * @device: The device to destroy.
 *
 * Idempotent: uses @hw_shutdown to ensure the teardown sequence runs
 * only once even if called from multiple paths (e.g., probe error +
 * remove).
 *
 * Teardown order:
 *   1. Set @hw_shutdown = true (prevents new ioctls).
 *   2. Deregister the miscdevice (prevents new opens).
 *   3. Iterate all queue pairs: stop, remove each HW queue, erase from
 *      xarray, and drop the xarray's ref.
 *   4. Destroy the xarray.
 *   5. Close the libqdma device handle.
 *
 * Note: the device structure itself is freed later by the kref callback
 * (slash_qdma_dev_release) when the last reference drops.
 */
static void slash_qdma_destroy_qdma_device(struct slash_qdma_dev *device)
{
    int err;

    if (!device) {
        return;
    }

    mutex_lock(&device->lock);
    if (device->hw_shutdown) {
        mutex_unlock(&device->lock);
        return;
    }
    device->hw_shutdown = true;
    mutex_unlock(&device->lock);

    /* Detach from PCI drvdata so no new lookups can find us. */
    pci_set_drvdata(device->pdev, NULL);

    /* Deregister miscdevice to prevent new file opens. */
    if (device->is_misc_registered) {
        misc_deregister(&device->misc);
        slash_qdma_id_release(pci_name(device->pdev));
        device->is_misc_registered = false;
    }

    mutex_lock(&device->lock);

    {
        /*
         * Tear down all remaining queue pairs.  This handles the case
         * where userspace did not cleanly delete its queues before the
         * device is removed (e.g., surprise removal or unclean exit).
         */
        struct slash_qdma_qpair_entry *entry;
        unsigned long index;
        unsigned int idx;

        xa_for_each(&device->qpairs, index, entry) {
            for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
                enum queue_type_t qtype = idx;
                u32 dir_bit = slash_qdma_qtype_to_dir(qtype);

                if (!(entry->dir_mask & dir_bit))
                    continue;

                slash_qdma_ioctl_qpair_rm_q(&device->misc, device, entry, qtype);
            }
            xa_erase(&device->qpairs, index);
            slash_qdma_qpair_put(entry);
        }
        xa_destroy(&device->qpairs);
    }

    /* Close the libqdma device handle, releasing HW resources. */
    if (device->have_qdma_handle) {
        SLASH_QDMA_OP_DEV_LOG(&device->pdev->dev,
                              "qdma_device_close start: handle=%lu\n",
                              device->qdma_handle);
        err = qdma_device_close(device->pdev, device->qdma_handle);
        if (err) {
            SLASH_QDMA_OP_DEV_LOG(&device->pdev->dev,
                                  "qdma_device_close failed: err=%d\n", err);
            dev_err(&device->pdev->dev, "Error in qdma_device_close: %d\n", err);
        } else {
            SLASH_QDMA_OP_DEV_LOG(&device->pdev->dev,
                                  "qdma_device_close done\n");
        }
        device->have_qdma_handle = false;
    }

    mutex_unlock(&device->lock);
}

/**
 * slash_qdma_dev_release() - kref release callback for the QDMA device.
 * @ref: kref embedded in the slash_qdma_dev being released.
 *
 * Called when the last reference drops (after both the miscdevice is
 * closed and all anon_inode fds are released).  Frees the dynamically
 * allocated miscdevice name/nodename strings and the device structure.
 */
static void slash_qdma_dev_release(struct kref *ref)
{
    struct slash_qdma_dev *device =
        container_of(ref, struct slash_qdma_dev, ref);

    mutex_destroy(&device->lock);

    if (device->misc.name) {
        kfree(device->misc.name);
    }

    if (device->misc.nodename) {
        kfree(device->misc.nodename);
    }

    kfree(device);
}

/* ─────────────────────────────────────────────────────────────────────
 * libqdma device configuration
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_conf_options() - Populate the qdma_dev_conf for device open.
 * @conf: Configuration structure to fill in.
 * @pdev: PCI device being opened.
 *
 * Sets up the libqdma device configuration with parameters tuned for
 * the V80 SLASH design:
 *
 *   - qsets_max = 256: maximum number of queue pairs (matches
 *     SLASH_QDMA_MAX_QPAIRS).
 *   - zerolen_dma = 0: zero-length transfers are disallowed.
 *   - master_pf = 1: this is the master physical function.
 *   - qdma_drv_mode = POLL_MODE: avoids interrupt overhead for
 *     streaming workloads.  The host polls HW-written completion
 *     status in memory instead of waiting for MSI-X interrupts.
 *   - msix_qvec_max = 32: Versal-specific MSI-X vector limit for
 *     queues.  Even though we use poll mode, libqdma still needs
 *     a non-zero value here for internal setup.
 *   - intr_rngsz = INTR_RING_SZ_4KB: interrupt ring size from the
 *     reference driver defaults.
 *   - bar_num_config = 0: BAR 0 is the configuration BAR.
 *   - bar_num_user / bar_num_bypass = -1: not used in this design.
 *   - qsets_base = -1: let libqdma auto-assign the queue set base.
 *   - All optional callbacks (ISR handlers, FLR resource free) are
 *     set to NULL since we operate in poll mode.
 */
static void slash_qdma_conf_options(struct qdma_dev_conf *conf, struct pci_dev *pdev)
{
    conf->pdev               = pdev;
    conf->qsets_max          = 256; /* Maximum number of queue paris. Might be lowered. TODO: tune */
    conf->zerolen_dma        = 0; /* Disallow 0-length transfers */
    conf->master_pf          = 1; /* This is the master PF */
    conf->intr_moderation    = 1;
    conf->vf_max             = 8;
    conf->intr_rngsz         = INTR_RING_SZ_4KB; // TODO: tune

    // Ask for as many queue MSI-X vectors as you'd like to dedicate to queues
    conf->msix_qvec_max      = 32; // For Versal
    conf->user_msix_qvec_max = 1;
    conf->data_msix_qvec_max = 5;

    conf->qdma_drv_mode      = POLL_MODE; // TODO: experiment with this
    conf->uld                = 0;

    conf->bar_num_config     = 0;
    conf->bar_num_user       = -1;
    conf->bar_num_bypass     = -1;
    conf->qsets_base         = -1;

    // Optional callbacks
    conf->fp_user_isr_handler = NULL;
    conf->fp_q_isr_top_dev    = NULL;
    conf->fp_flr_free_resource= NULL;
    conf->debugfs_dev_root    = NULL;
}

/* ─────────────────────────────────────────────────────────────────────
 * Miscdevice file operations (management interface)
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_fop_ioctl() - Dispatch ioctls on the QDMA control miscdevice.
 * @file: Open file for the miscdevice.
 * @op:   Ioctl command number.
 * @arg:  User-space argument pointer.
 *
 * Routes incoming ioctls to the appropriate handler:
 *   - SLASH_QDMA_IOCTL_INFO:       query QDMA capabilities
 *   - SLASH_QDMA_IOCTL_QPAIR_ADD:  allocate a new queue pair
 *   - SLASH_QDMA_IOCTL_Q_OP:       start/stop/delete a queue pair
 *   - SLASH_QDMA_IOCTL_QPAIR_GET_FD: obtain an I/O fd for a queue pair
 *
 * All paths check @hw_shutdown before proceeding to reject ioctls
 * that arrive during or after device teardown.
 *
 * Return: 0 or positive fd on success, negative errno on failure.
 */
static long slash_qdma_fop_ioctl(struct file *file, unsigned int op, unsigned long arg)
{
    struct slash_qdma_client *client = file->private_data;
    struct slash_qdma_dev *qdma_dev;
    struct miscdevice *misc;
    void __user *uarg = (void __user *)arg;
    long ret = 0;

    if (!client || !client->qdma_dev)
        return -ENODEV;

    qdma_dev = client->qdma_dev;
    misc = &qdma_dev->misc;

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev, "ioctl op=0x%x\n", op);

    /* Early rejection if the device is shutting down. */
    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    mutex_unlock(&qdma_dev->lock);

    switch (op) {
    case SLASH_QDMA_IOCTL_INFO:
        ret = slash_qdma_ioctl_info_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_QPAIR_ADD:
        ret = slash_qdma_ioctl_qpair_add_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_Q_OP:
        ret = slash_qdma_ioctl_qpair_op_w(misc, qdma_dev, uarg);
        break;

    case SLASH_QDMA_IOCTL_QPAIR_GET_FD:
        ret = slash_qdma_ioctl_qpair_get_fd_w(misc, client, uarg);
        break;

    case SLASH_QDMA_IOCTL_BUF_REGISTER:
        ret = slash_qdma_ioctl_buf_register_w(misc, client, uarg);
        break;

    case SLASH_QDMA_IOCTL_BUF_UNREGISTER:
        ret = slash_qdma_ioctl_buf_unregister_w(misc, client, uarg);
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

/**
 * slash_qdma_fop_open() - Open handler for the QDMA control miscdevice.
 * @inode: Inode of the device node.
 * @file:  File being opened.
 *
 * The misc framework sets file->private_data to the miscdevice before
 * calling open.  We use container_of to recover the slash_qdma_dev,
 * take a device reference, and stash the device pointer in private_data
 * so that subsequent ioctl/release calls can find it directly.
 *
 * Return: 0 on success, -ENODEV if the device is shutting down.
 */
static int slash_qdma_fop_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct slash_qdma_dev *qdma_dev =
        container_of(misc, struct slash_qdma_dev, misc);
    struct slash_qdma_client *client;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    kref_get(&qdma_dev->ref);
    mutex_unlock(&qdma_dev->lock);

    /*
     * Allocate a per-open client context to own any buffers registered
     * through this fd.  The control fd holds the initial client ref; it
     * is dropped (and the buffers torn down) in slash_qdma_fop_release().
     */
    client = kzalloc(sizeof(*client), GFP_KERNEL);
    if (!client) {
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        return -ENOMEM;
    }

    kref_init(&client->ref);
    client->qdma_dev = qdma_dev;
    xa_init_flags(&client->buffers, XA_FLAGS_ALLOC);

    file->private_data = client;

    return 0;
}

/**
 * slash_qdma_fop_release() - Release handler for the QDMA control miscdevice.
 * @inode: Inode of the device node.
 * @file:  File being closed.
 *
 * Drops the device reference acquired in open.  If this is the last
 * reference, the device structure is freed.
 *
 * Return: Always 0.
 */
static int slash_qdma_fop_release(struct inode *inode, struct file *file)
{
    struct slash_qdma_client *client = file->private_data;
    struct slash_qdma_buf *buf;
    unsigned long index;

    if (!client)
        return 0;

    /*
     * Auto-unregister any buffers the client forgot (or had no chance) to
     * release.  Remove each from the lookup table first so no new transfer
     * can find it, then drop the table's reference.  Buffers with an
     * in-flight transfer stay alive until that transfer releases its ref.
     */
    xa_for_each(&client->buffers, index, buf) {
        xa_erase(&client->buffers, index);
        slash_qdma_buf_put(buf);
    }
    xa_destroy(&client->buffers);

    kref_put(&client->ref, slash_qdma_client_release);

    file->private_data = NULL;

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * IOCTL: info
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_ioctl_info_w() - Wrapper for the QDMA info ioctl.
 * @misc:     Miscdevice handle.
 * @qdma_dev: QDMA device.
 * @uarg:     User-space pointer to a slash_qdma_info struct.
 *
 * Implements the versioned copy-in / copy-out pattern:
 *   1. Read the leading @size field to learn how large the caller's
 *      struct is (ABI forward/backward compatibility).
 *   2. Fill the kernel-side struct via slash_qdma_ioctl_info().
 *   3. Copy back only min(user_size, kernel_size) bytes.
 *
 * Return: 0 on success, -EFAULT on copy failure, -ENODEV if shutting down.
 */
static int slash_qdma_ioctl_info_w(struct miscdevice *misc,
                                    struct slash_qdma_dev *qdma_dev,
                                    void __user *uarg)
{
    struct slash_qdma_info info;
    u32 user_size = 0;
    size_t copy_size;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_INFO_MIN_SIZE) {
        dev_warn(misc->this_device,
                 "qdma: INFO size too small (%u)\n", user_size);
        return -EINVAL;
    }

    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    slash_qdma_ioctl_info(misc, qdma_dev, &info);
    mutex_unlock(&qdma_dev->lock);

    copy_size = min_t(size_t, user_size, sizeof(info));
    if (copy_to_user(uarg, &info, copy_size))
        return -EFAULT;
    if (user_size > sizeof(info)) {
        if (clear_user((void __user *)((unsigned long)uarg + sizeof(info)),
                       user_size - sizeof(info)))
            return -EFAULT;
    }

    return 0;
}

/**
 * slash_qdma_ioctl_info() - Populate QDMA capability information.
 * @misc:      Miscdevice handle (unused).
 * @qdma_dev:  QDMA device (unused for now).
 * @qdma_info: [out] Structure to fill with capability data.
 *
 * Currently returns zeroes for all fields.  This is a placeholder for
 * future capability reporting (e.g., querying qdma_device_capabilities).
 */
static void slash_qdma_ioctl_info(struct miscdevice *misc,
                                  struct slash_qdma_dev *qdma_dev,
                                  struct slash_qdma_info *qdma_info)
{
    (void) misc;
    (void) qdma_dev;

    qdma_info->qsets_max = 0;
    qdma_info->msix_qvecs = 0;
    qdma_info->vf_max = 0;
    qdma_info->caps = 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * IOCTL: qpair add
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_ioctl_qpair_add_w() - Wrapper for the qpair-add ioctl.
 * @misc:     Miscdevice handle.
 * @qdma_dev: QDMA device.
 * @uarg:     User-space pointer to a slash_qdma_qpair_add struct.
 *
 * Validates userspace inputs:
 *   - @dir_mask must be non-zero, contain only known bits, and not include CMPT
 *     (completion queues are not yet supported).
 *   - @mode must be MM; streaming mode (ST) is not yet supported.
 *   - Ring size indices must be in [0, 15] (CSR table range).
 *
 * On success, the kernel-assigned @qid is written back to userspace.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_qpair_add_w(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         void __user *uarg)
{
    struct slash_qdma_qpair_add req;
    __u32 user_size = 0;
    size_t copy_size;
    u32 dir_mask;
    int err;

    /*
     * First, fetch the size field from userspace so we can
     * safely handle callers built against older or newer
     * versions of the struct.
     */
    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_QPAIR_ADD_MIN_SIZE) {
        dev_warn(misc->this_device,
                 "qdma: QPAIR_ADD size too small (%u)\n", user_size);
        return -EINVAL;
    }

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    /* Completion queues are not yet supported. */
    if (req.dir_mask & SLASH_QDMA_DIR_CMPT)
        return -EOPNOTSUPP;

    /* Validate direction mask: must be non-zero and contain only known bits. */
    dir_mask = req.dir_mask & SLASH_QDMA_DIR_MASK;
    if (!dir_mask || dir_mask != req.dir_mask)
        return -EINVAL;

    /* Streaming mode is not yet supported; only memory-mapped mode is accepted. */
    if (req.mode == QDMA_Q_MODE_ST)
        return -EOPNOTSUPP;
    if (req.mode != QDMA_Q_MODE_MM)
        return -EINVAL;

    /*
     * Ring size fields are CSR table indices (0-15), not raw descriptor
     * counts.  Each index selects a pre-programmed ring depth from the
     * global CSR ring-size table.
     */
    if (req.h2c_ring_sz >= 16 || req.c2h_ring_sz >= 16 || req.cmpt_ring_sz >= 16)
        return -EINVAL;

    /* Validate the per-queue AXI-MM channel selection. */
    if (req.mm_channel != SLASH_QDMA_MM_CHANNEL_AUTO &&
        req.mm_channel != SLASH_QDMA_MM_CHANNEL_0 &&
        req.mm_channel != SLASH_QDMA_MM_CHANNEL_1)
        return -EINVAL;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    err = slash_qdma_ioctl_qpair_add(misc, qdma_dev, &req);
    mutex_unlock(&qdma_dev->lock);

    if (err)
        return err;

    /*
     * On success, update the size field to reflect the
     * kernel's view of the struct and copy back only as
     * many bytes as the caller originally provided.
     */
    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size))
        return -EFAULT;
    if (user_size > sizeof(req)) {
        if (clear_user((void __user *)((unsigned long)uarg + sizeof(req)),
                       user_size - sizeof(req)))
            return -EFAULT;
    }

    return err;
}

/**
 * slash_qdma_ioctl_qpair_add() - Allocate a qpair and add its constituent queues.
 * @misc:     Miscdevice handle.
 * @qdma_dev: QDMA device.
 * @req:      Add request (dir_mask, mode, ring sizes); @qid is set on success.
 *
 * Allocates a slash_qdma_qpair_entry, inserts it into the xarray (which
 * auto-assigns the qpair ID), and then iterates over the requested
 * directions to add each individual HW queue.  If any queue addition
 * fails, all previously-added queues are rolled back and the xarray
 * entry is removed.
 *
 * The xarray-assigned ID is used as the QDMA queue index for all queues
 * in the pair, so H2C queue N and C2H queue N share the same index.
 * Any qid value provided by userspace in the request is ignored.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_qpair_add(struct miscdevice *misc,
                                      struct slash_qdma_dev *qdma_dev,
                                      struct slash_qdma_qpair_add *req)
{
    struct slash_qdma_qpair_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    unsigned int idx;
    bool added[SLASH_QDMA_QTYPE_COUNT] = {0};
    int ret = 0;

    if (!entry)
        return -ENOMEM;

    entry->mode = req->mode;
    entry->irq_mode = 0;
    entry->irq_vector = 0;

    /* Initialise all queue handles to invalid (not yet added). */
    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++)
        entry->qhndl[idx] = QDMA_QUEUE_IDX_INVALID;

    /*
     * Allocate a new qpair ID in the xarray and use it as the
     * QDMA queue index for all queues in this pair. Any qid
     * value provided by userspace is ignored.
     */
    ret = slash_qdma_qpair_insert(qdma_dev, entry, &req->qid);
    if (ret) {
        dev_err(&qdma_dev->pdev->dev,
                "qdma: qpair insert failed: %d\n", ret);
        kfree(entry);
        return ret;
    }

    /* Add each requested direction's HW queue. */
    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        enum queue_type_t qtype = idx;
        u32 dir_bit = slash_qdma_qtype_to_dir(qtype);

        if (!(req->dir_mask & dir_bit))
            continue;

        ret = slash_qdma_ioctl_qpair_add_q(misc, qdma_dev, req, entry, qtype);
        if (ret)
            goto rollback;

        added[idx] = true;
    }

    return 0;

rollback:
    /* Undo any queues that were successfully added before the failure. */
    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        if (added[idx])
            slash_qdma_ioctl_qpair_rm_q(misc, qdma_dev, entry, idx);
    }

    slash_qdma_qpair_remove(qdma_dev, req->qid);

    return ret;
}

/**
 * slash_qdma_ioctl_qpair_add_q() - Add a single HW queue to a queue pair.
 * @misc:     Miscdevice handle (for error logging context).
 * @qdma_dev: QDMA device.
 * @req:      The add request (provides queue index, mode, and ring sizes).
 * @entry:    The qpair entry to attach the new queue to.
 * @qtype:    Which queue type to add (Q_H2C, Q_C2H, or Q_CMPT).
 *
 * Fills a qdma_queue_conf structure and calls qdma_queue_add().  The
 * configuration fields deserve detailed explanation:
 *
 *   - qconf.qidx: set to the xarray-assigned qpair ID so all queues
 *     in a pair share the same HW queue index.
 *   - qconf.st: 1 for streaming mode (QDMA_Q_MODE_ST), 0 for memory-
 *     mapped (QDMA_Q_MODE_MM).  Streaming uses AXI-Stream for data
 *     transfer; MM uses AXI Memory Mapped.
 *   - qconf.irq_en = 0: interrupts disabled — we use poll mode.
 *   - qconf.cmpl_en_intr = 0: no completion interrupts — poll mode.
 *   - qconf.cmpl_trig_mode = TRIG_MODE_DISABLE: no automatic completion
 *     trigger; the host explicitly polls for completion status.
 *   - qconf.wb_status_en = 1: enables HW write-back of completion status
 *     to host memory, which is how the poll-mode driver detects transfer
 *     completion.
 *   - qconf.cmpl_status_acc_en = 1: accumulate completion status entries
 *     (required for poll-mode operation per the reference driver).
 *   - qconf.cmpl_status_pend_chk = 1: check for pending completions
 *     (required for poll-mode operation per the reference driver).
 *   - qconf.cmpl_stat_en = 1: enable completion status generation
 *     (required for poll-mode operation per the reference driver).
 *   - qconf.aperture_size = 0: disables libqdma keyhole mode so MM
 *     transfers advance linearly through endpoint memory.  Non-zero
 *     values are keyhole apertures and wrap addresses within that window.
 *   - qconf.desc_rng_sz_idx: CSR table index (0-15) selecting the
 *     descriptor ring depth.  Not a raw descriptor count — the actual
 *     count is looked up from the global CSR ring-size table.
 *   - qconf.cmpl_rng_sz_idx: same as desc_rng_sz_idx but for the
 *     completion ring (C2H and CMPT queues only).
 *   - qconf.cmpl_desc_sz = CMPT_DESC_SZ_16B: 16-byte completion
 *     descriptors (C2H and CMPT queues only).
 *
 * For CMPT-type queues, streaming mode is forced off (qconf.st = 0)
 * because the completion queue is always memory-mapped regardless of
 * the data queue mode.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_qpair_add_q(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         struct slash_qdma_qpair_add *req,
                                         struct slash_qdma_qpair_entry *entry,
                                         enum queue_type_t qtype)
{
    u32 dir_bit = slash_qdma_qtype_to_dir(qtype);
    struct qdma_queue_conf qconf = {0};
    char errbuf[128] = {0};
    u32 dir_mask = req->dir_mask;
    int err;
    unsigned long qhndl = QDMA_QUEUE_IDX_INVALID;

    if (!(dir_mask & dir_bit))
        return -EINVAL;

    /* --- Common queue configuration (all directions) --- */
    qconf.qidx = req->qid;                          /* Use xarray-assigned ID as HW queue index */
    qconf.q_type = qtype;
    qconf.st = (req->mode == QDMA_Q_MODE_ST);       /* Streaming vs memory-mapped */
    qconf.irq_en = 0;                               /* Poll mode: no interrupts */
    qconf.cmpl_en_intr = 0;                         /* Poll mode: no completion interrupts */
    qconf.cmpl_trig_mode = TRIG_MODE_DISABLE;       /* No auto-trigger; we poll explicitly */

    qconf.wb_status_en = 1;                         /* HW writes completion status to host memory */
    qconf.cmpl_status_acc_en = 1;                   /* Accumulate completion status (poll-mode req) */
    qconf.cmpl_status_pend_chk = 1;                 /* Check pending completions (poll-mode req) */
    qconf.cmpl_stat_en = 1;                         /* Enable completion status generation */

    qconf.aperture_size = 0;                        /* Linear MM addressing; non-zero enables keyhole mode */
    /*
     * CPM5 exposes two MM channels.  The per-queue mm_channel selection
     * (validated in slash_qdma_ioctl_qpair_add_w) chooses the channel: AUTO
     * stripes across channels by (qid & 1); CHANNEL_0/CHANNEL_1 pin to a single
     * channel.  libqdma mirrors mm_channel into the SW-context host_id, so this
     * also selects the programmed Host Profile: channel 0 -> Host Profile 0
     * (NoC Channel 0), channel 1 -> Host Profile 1 (NoC Channel 1).  See
     * slash_qdma_program_host_profiles().
     */
    switch (req->mm_channel) {
    case SLASH_QDMA_MM_CHANNEL_0:
        qconf.mm_channel = 0;
        break;
    case SLASH_QDMA_MM_CHANNEL_1:
        qconf.mm_channel = 1;
        break;
    case SLASH_QDMA_MM_CHANNEL_AUTO:
    default:
        qconf.mm_channel = req->qid & 1;
        break;
    }

    /* --- Per-direction ring configuration --- */
    switch (qtype) {
    case Q_H2C:
        qconf.desc_rng_sz_idx = req->h2c_ring_sz;   /* CSR table index for H2C descriptor ring */
        break;
    case Q_C2H:
        qconf.desc_rng_sz_idx = req->c2h_ring_sz;   /* CSR table index for C2H descriptor ring */
        qconf.cmpl_rng_sz_idx = req->cmpt_ring_sz;  /* CSR table index for C2H completion ring */
        qconf.cmpl_desc_sz = CMPT_DESC_SZ_16B;      /* 16-byte completion descriptors */
        break;
    case Q_CMPT:
        qconf.st = 0;                               /* CMPT queue is always memory-mapped */
        qconf.desc_rng_sz_idx = req->cmpt_ring_sz;
        qconf.cmpl_rng_sz_idx = req->cmpt_ring_sz;
        qconf.cmpl_desc_sz = CMPT_DESC_SZ_16B;      /* 16-byte completion descriptors */
        qconf.cmpl_en_intr = 0;                      /* Redundant but explicit: no CMPT interrupts */
        break;
    default:
        break;
    }

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "queue add qid=%u type=%u mode=%u mm_channel=%u (req=%u)\n",
                          req->qid, qtype, req->mode, qconf.mm_channel,
                          req->mm_channel);
    err = qdma_queue_add(qdma_dev->qdma_handle, &qconf, &qhndl,
                            errbuf, sizeof(errbuf));
    if (err) {
        SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                              "qdma_queue_add failed: qid=%u type=%u err=%d (%s)\n",
                              req->qid, qtype, err, errbuf);
        dev_err(&qdma_dev->pdev->dev,
                "qdma: queue add failed (qid=%u, type=%u): %d (%s)\n",
                req->qid, qtype, err, errbuf);
        return err;
    }
    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "qdma_queue_add done: qid=%u type=%u qhndl=%lu\n",
                          req->qid, qtype, qhndl);

    /* Record the handle and mark this direction as active. */
    entry->qhndl[qtype] = qhndl;
    entry->dir_mask |= dir_bit;

    return 0;
}

/**
 * slash_qdma_ioctl_qpair_rm_q() - Remove a single HW queue from a queue pair.
 * @misc:     Miscdevice handle (for logging context).
 * @qdma_dev: QDMA device.
 * @entry:    The qpair entry to remove the queue from.
 * @qtype:    Which queue type to remove (Q_H2C, Q_C2H, or Q_CMPT).
 *
 * Uses slash_qdma_queue_remove_safe() to handle all possible HW queue
 * states (online, enabled, or already disabled).  On completion, the
 * queue handle is set to QDMA_QUEUE_IDX_INVALID and the direction bit
 * is cleared from the entry's dir_mask.
 *
 * Errors are logged but not propagated — this is best-effort cleanup
 * used during teardown.
 */
static void slash_qdma_ioctl_qpair_rm_q(struct miscdevice *misc,
                                         struct slash_qdma_dev *qdma_dev,
                                         struct slash_qdma_qpair_entry *entry,
                                         enum queue_type_t qtype)
{
    unsigned long qhndl = entry->qhndl[qtype];
    char errbuf[128] = {0};
    int err;

    /* If the handle is already invalid, just clear state and return. */
    if (!slash_qdma_qhndl_is_valid(qhndl)) {
        entry->qhndl[qtype] = QDMA_QUEUE_IDX_INVALID;
        entry->dir_mask &= ~slash_qdma_qtype_to_dir(qtype);
        return;
    }

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "queue_remove_safe start: type=%u qhndl=%lu\n",
                          qtype, qhndl);
    err = slash_qdma_queue_remove_safe(qdma_dev->qdma_handle, qhndl,
                                       errbuf, sizeof(errbuf));

    if (err) {
        SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                              "queue_remove_safe failed: type=%u qhndl=%lu err=%d (%s)\n",
                              qtype, qhndl, err, errbuf);
        dev_err(&qdma_dev->pdev->dev,
                "qdma: queue remove failed (type=%u): %d (%s)\n",
                qtype, err, errbuf);
        return;
    }
    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "queue_remove_safe done: type=%u qhndl=%lu\n",
                          qtype, qhndl);

    entry->qhndl[qtype] = QDMA_QUEUE_IDX_INVALID;
    entry->dir_mask &= ~slash_qdma_qtype_to_dir(qtype);
}

/* ─────────────────────────────────────────────────────────────────────
 * IOCTL: qpair op (start / stop / delete)
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_ioctl_qpair_op_w() - Wrapper for the qpair operation ioctl.
 * @misc:     Miscdevice handle.
 * @qdma_dev: QDMA device.
 * @uarg:     User-space pointer to a slash_qdma_qpair_op struct.
 *
 * Handles the versioned copy-in / copy-out pattern and validates that
 * the requested operation is within the known range.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_qpair_op_w(struct miscdevice *misc,
                                       struct slash_qdma_dev *qdma_dev,
                                       void __user *uarg)
{
    struct slash_qdma_qpair_op req;
    __u32 user_size = 0;
    size_t copy_size;
    int ret;

    /*
     * First, fetch the size field from userspace so we can
     * safely handle callers built against older or newer
     * versions of the struct.
     */
    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_QPAIR_OP_MIN_SIZE) {
        dev_warn(misc->this_device,
                 "qdma: Q_OP size too small (%u)\n", user_size);
        return -EINVAL;
    }

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    if (req.op > SLASH_QDMA_QUEUE_OP_DEL)
        return -EINVAL;

    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }
    ret = slash_qdma_ioctl_qpair_op(misc, qdma_dev, &req);
    mutex_unlock(&qdma_dev->lock);

    if (ret)
        return ret;

    /*
     * On success, update the size field to reflect the
     * kernel's view of the struct and copy back only as
     * many bytes as the caller originally provided.
     */
    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size))
        return -EFAULT;
    if (user_size > sizeof(req)) {
        if (clear_user((void __user *)((unsigned long)uarg + sizeof(req)),
                       user_size - sizeof(req)))
            return -EFAULT;
    }

    return ret;
}

/**
 * slash_qdma_ioctl_qpair_op() - Dispatch a lifecycle operation on a queue pair.
 * @misc:     Miscdevice handle (unused, present for API consistency).
 * @qdma_dev: QDMA device.
 * @req:      Operation request (@qid identifies the target, @op selects
 *            the action).
 *
 * Looks up the qpair entry and dispatches to slash_qdma_ioctl_qpair_op_apply()
 * with the appropriate libqdma function pointer:
 *
 *   - START: qdma_queue_start (stop_on_err=true — fail fast)
 *   - STOP:  qdma_queue_stop  (stop_on_err=true — fail fast)
 *   - DEL:   slash_qdma_queue_remove_safe (stop_on_err=false — best effort,
 *            try to remove as many queues as possible even if one fails);
 *            on success, also removes the entry from the xarray.
 *
 * Return: 0 on success, -ENOENT if qpair not found, other negative errno
 *         from the underlying libqdma call.
 */
static int slash_qdma_ioctl_qpair_op(struct miscdevice *misc,
                                     struct slash_qdma_dev *qdma_dev,
                                     struct slash_qdma_qpair_op *req)
{
    struct slash_qdma_qpair_entry *entry;
    int ret = 0;

    (void) misc;

    if (!qdma_dev->have_qdma_handle)
        return -ENODEV;

    entry = slash_qdma_qpair_lookup(qdma_dev, req->qid);
    if (!entry)
        return -ENOENT;

    switch (req->op) {
    case SLASH_QDMA_QUEUE_OP_START:
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          qdma_queue_start,
                                          "start", true);
        break;
    case SLASH_QDMA_QUEUE_OP_STOP:
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          qdma_queue_stop,
                                          "stop", true);
        break;
    case SLASH_QDMA_QUEUE_OP_DEL:
        /*
         * For delete, use stop_on_err=false to attempt removal of all
         * directions even if one fails, then remove from the xarray.
         */
        ret = slash_qdma_ioctl_qpair_op_apply(qdma_dev, entry, req,
                                          slash_qdma_queue_remove_safe,
                                          "remove", false);
        if (!ret)
            slash_qdma_qpair_remove(qdma_dev, req->qid);
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

/**
 * slash_qdma_ioctl_qpair_op_apply() - Apply a lifecycle operation to all queues in a pair.
 * @qdma_dev:    QDMA device.
 * @entry:       Queue pair entry.
 * @req:         Operation request (used for @qid in log messages).
 * @fn:          The libqdma function to call per queue (e.g., qdma_queue_start).
 * @op_name:     Human-readable operation name for log messages.
 * @stop_on_err: If true, return immediately on the first error.
 *               If false, continue through all directions and return
 *               the first error encountered.
 *
 * Iterates over all queue types (H2C, C2H, CMPT).  For each direction
 * that is active in the entry's dir_mask, calls @fn with the corresponding
 * queue handle.
 *
 * Return: 0 if all calls succeed, otherwise the first negative errno.
 */
static int slash_qdma_ioctl_qpair_op_apply(struct slash_qdma_dev *qdma_dev,
                                           struct slash_qdma_qpair_entry *entry,
                                           struct slash_qdma_qpair_op *req,
                                           slash_qdma_queue_cmd_fn fn,
                                           const char *op_name,
                                           bool stop_on_err)
{
    int idx;
    int first_err = 0;

    for (idx = 0; idx < SLASH_QDMA_QTYPE_COUNT; idx++) {
        enum queue_type_t qtype = idx;
        u32 dir_bit = slash_qdma_qtype_to_dir(qtype);
        char errbuf[128] = {0};
        int err;

        /* Skip directions not present in this queue pair. */
        if (!(entry->dir_mask & dir_bit) ||
            !slash_qdma_qhndl_is_valid(entry->qhndl[qtype]))
            continue;

        SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                              "qdma_queue_%s start: qid=%u type=%u qhndl=%lu\n",
                              op_name, req->qid, qtype, entry->qhndl[qtype]);
        err = fn(qdma_dev->qdma_handle, entry->qhndl[qtype],
                 errbuf, (int)sizeof(errbuf));
        if (err) {
            SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                                  "qdma_queue_%s failed: qid=%u type=%u qhndl=%lu err=%d (%s)\n",
                                  op_name, req->qid, qtype, entry->qhndl[qtype], err, errbuf);
            dev_err(&qdma_dev->pdev->dev,
                    "qdma: queue %s failed (qid=%u, type=%u): %d (%s)\n",
                    op_name, req->qid, qtype, err, errbuf);

            if (stop_on_err)
                return err;

            if (!first_err)
                first_err = err;
        } else {
            SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                                  "qdma_queue_%s done: qid=%u type=%u qhndl=%lu\n",
                                  op_name, req->qid, qtype, entry->qhndl[qtype]);
        }
    }

    return first_err;
}

/* ─────────────────────────────────────────────────────────────────────
 * DMA I/O: user buffer mapping, SGL construction, and transfer
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_iocb_release() - Free resources in an I/O control block.
 * @iocb: The IOCB to clean up.
 *
 * Frees the combined SGL + page-pointer allocation and clears the
 * pointers.  Does not unpin pages — that must be done separately via
 * slash_qdma_unmap_user_buf() before calling this.
 */
static inline void slash_qdma_iocb_release(struct slash_qdma_io_cb *iocb)
{
    if (iocb->pages)
        iocb->pages = NULL;

    kvfree(iocb->sgl);
    iocb->sgl = NULL;
    iocb->buf = NULL;
}

/**
 * slash_qdma_unmap_user_buf() - Unpin user pages after a DMA transfer.
 * @iocb:  I/O control block with pinned pages.
 * @write: Transfer direction from the device's perspective.  If false
 *         (i.e., a C2H/read transfer), the pages were written to by the
 *         device and must be marked dirty so the VM knows the page
 *         contents have changed.
 *
 * Iterates over pinned pages, marks them dirty if this was a read (C2H)
 * transfer (because the device wrote data into those user pages), and
 * releases each page reference acquired by get_user_pages_fast().
 */
static void slash_qdma_unmap_user_buf(struct slash_qdma_io_cb *iocb, bool write)
{
    int i;

    if (!iocb->pages || !iocb->pages_nr)
        return;

    for (i = 0; i < iocb->pages_nr; i++) {
        if (iocb->pages[i]) {
            /*
             * For C2H (read) transfers (!write), the device wrote into
             * these user pages, so mark them dirty to inform the VM.
             */
            if (!write)
                set_page_dirty(iocb->pages[i]);
            put_page(iocb->pages[i]);
        } else {
            break;
        }
    }

    if (i != iocb->pages_nr)
        pr_err("slash: qdma: sgl pages %d/%u.\n", i, iocb->pages_nr);

    iocb->pages_nr = 0;
}

static int slash_qdma_iocb_alloc_sgl(struct slash_qdma_io_cb *iocb,
                                     unsigned int entries)
{
    size_t entry_size = sizeof(struct qdma_sw_sg) + sizeof(struct page *);
    struct qdma_sw_sg *sg;

    if (!entries || entries > SIZE_MAX / entry_size)
        return -EINVAL;

    /*
     * A large base-page transfer needs one entry per 4 KiB page (e.g. ~5 MiB
     * of SGL for a 512 MiB transfer), which exceeds kmalloc's limit, so use
     * kvcalloc().  The SGL is only ever touched by the CPU (libqdma DMA-maps
     * the pages it references), so a vmalloc-backed allocation is fine.
     */
    sg = kvcalloc(entries, entry_size, GFP_KERNEL);
    if (!sg) {
        pr_err("slash: qdma: sgl allocation failed for %u entries\n",
               entries);
        return -ENOMEM;
    }

    iocb->sgl = sg;
    iocb->pages = (struct page **)(sg + entries);
    return 0;
}

static bool slash_qdma_page_is_base_page(struct page *page)
{
    return !PageCompound(page);
}

static bool slash_qdma_page_is_2m_hugetlb_head(struct page *page)
{
#ifdef CONFIG_HUGETLB_PAGE
    struct page *head = compound_head(page);

    return page == head &&
           PageHuge(head) &&
           compound_order(head) == get_order(SLASH_QDMA_HUGEPAGE_SIZE);
#else
    return false;
#endif
}

static int slash_qdma_map_user_base_pages_to_sgl(struct slash_qdma_io_cb *iocb,
                                                 bool write)
{
    unsigned long addr = (unsigned long)iocb->buf;
    size_t entries = iocb->len / PAGE_SIZE;
    unsigned int pinned = 0;
    unsigned int i;
    int rv;

    if ((iocb->len % PAGE_SIZE) != 0 || entries == 0 || entries > UINT_MAX)
        return -EINVAL;

    rv = slash_qdma_iocb_alloc_sgl(iocb, (unsigned int)entries);
    if (rv)
        return rv;

    /*
     * Pin every base page in the span.  get_user_pages_fast() may return
     * fewer pages than requested, so loop (in bounded batches) until the
     * whole buffer is pinned.
     */
    while (pinned < entries) {
        unsigned int want = min_t(unsigned int,
                                  (unsigned int)entries - pinned,
                                  SLASH_QDMA_GUP_BATCH);
        int got = get_user_pages_fast(addr + (size_t)pinned * PAGE_SIZE,
                                      (int)want, 1 /* write */,
                                      iocb->pages + pinned);

        if (got <= 0) {
            pr_err("slash: qdma: unable to pin 4 KiB user pages %u/%zu, %d\n",
                   pinned, entries, got);
            rv = (got < 0) ? got : -EFAULT;
            goto err_out;
        }

        pinned += (unsigned int)got;
        iocb->pages_nr = pinned;
    }

    for (i = 0; i < entries; i++) {
        struct qdma_sw_sg *sg = &iocb->sgl[i];

        if (!slash_qdma_page_is_base_page(iocb->pages[i])) {
            pr_err("slash: qdma: 4 KiB transfer page %u/%zu is not backed by a base page\n",
                   i, entries);
            rv = -EINVAL;
            goto err_out;
        }

        flush_dcache_page(iocb->pages[i]);

        sg->next = (i + 1 < entries) ? &iocb->sgl[i + 1] : NULL;
        sg->pg = iocb->pages[i];
        sg->offset = 0;
        sg->len = PAGE_SIZE;
        sg->dma_addr = 0UL;
    }

    SLASH_QDMA_OP_LOG("user transfer path=base-4k addr=0x%lx len=%zu pages=%zu write=%d\n",
                      addr, iocb->len, entries, write);

    return 0;

err_out:
    slash_qdma_unmap_user_buf(iocb, write);
    slash_qdma_iocb_release(iocb);
    return rv;
}

static int slash_qdma_map_user_huge_page_to_sgl(struct slash_qdma_io_cb *iocb,
                                                bool write)
{
    unsigned long addr = (unsigned long)iocb->buf;
    size_t huge_pages = iocb->len / SLASH_QDMA_HUGEPAGE_SIZE;
    unsigned int desc_size = READ_ONCE(qdma_huge_desc_size);
    unsigned int descs_per_page;
    size_t entries;
    unsigned int i;
    unsigned int sg_idx = 0;
    int rv;

    if ((iocb->len % SLASH_QDMA_HUGEPAGE_SIZE) != 0 ||
        huge_pages == 0 || huge_pages > UINT_MAX)
        return -EINVAL;

    if (desc_size < PAGE_SIZE ||
        desc_size > SLASH_QDMA_HUGEPAGE_SIZE ||
        !IS_ALIGNED(desc_size, PAGE_SIZE) ||
        (SLASH_QDMA_HUGEPAGE_SIZE % desc_size) != 0)
        return -EINVAL;

    descs_per_page = SLASH_QDMA_HUGEPAGE_SIZE / desc_size;
    if (huge_pages > UINT_MAX / descs_per_page)
        return -EINVAL;
    entries = huge_pages * descs_per_page;

    rv = slash_qdma_iocb_alloc_sgl(iocb, (unsigned int)entries);
    if (rv)
        return rv;

    for (i = 0; i < huge_pages; i++) {
        unsigned long curr_addr = addr + (i * SLASH_QDMA_HUGEPAGE_SIZE);
        struct page *page = NULL;
        unsigned int j;

        rv = get_user_pages_fast(curr_addr, 1, 1 /* write */, &page);
        if (rv != 1) {
            pr_err("slash: qdma: unable to pin 2 MiB user page %u/%zu, %d\n",
                   i, huge_pages, rv);
            rv = rv < 0 ? rv : -EFAULT;
            goto err_out;
        }

        if (!slash_qdma_page_is_2m_hugetlb_head(page)) {
            pr_err("slash: qdma: 2 MiB transfer page %u/%zu is not backed by a 2 MiB hugetlb head page\n",
                   i, huge_pages);
            put_page(page);
            rv = -EINVAL;
            goto err_out;
        }

        flush_dcache_page(page);

        for (j = 0; j < descs_per_page; j++, sg_idx++) {
            struct qdma_sw_sg *sg = &iocb->sgl[sg_idx];

            /*
             * The first segment consumes the GUP reference.  Additional
             * descriptors over the same hugetlb page take explicit references
             * so slash_qdma_unmap_user_buf() can release one page ref per SGL
             * entry without special casing repeated pages.
             */
            if (j != 0)
                get_page(page);

            iocb->pages[sg_idx] = page;
            iocb->pages_nr = sg_idx + 1;

            sg->next = (sg_idx + 1 < entries) ? &iocb->sgl[sg_idx + 1] : NULL;
            sg->pg = page;
            sg->offset = j * desc_size;
            sg->len = desc_size;
            sg->dma_addr = 0UL;
        }
    }

    SLASH_QDMA_OP_LOG("user transfer path=hugetlb-2m addr=0x%lx len=%zu pages=%zu desc_size=%u descs=%zu write=%d\n",
                      addr, iocb->len, huge_pages, desc_size, entries, write);

    return 0;

err_out:
    slash_qdma_unmap_user_buf(iocb, write);
    slash_qdma_iocb_release(iocb);
    return rv;
}

/**
 * slash_qdma_map_user_buf_to_sgl() - Pin a user buffer and build its SGL.
 * @iocb:  I/O control block.  @iocb->buf and @iocb->len must be set.
 * @write: Transfer direction (true = H2C write, false = C2H read).
 *
 * The buffer must be page-aligned and a whole number of 4 KiB pages.  It is
 * mapped as either:
 *   - a span of 2 MiB hugetlb pages (when it is 2 MiB-aligned, a multiple of
 *     2 MiB, and actually backed by hugetlb pages), or
 *   - a span of 4 KiB base pages (every other accepted case).
 *
 * Each page becomes one SGL entry / one DMA descriptor, and the whole span is
 * submitted to libqdma as a single request.
 *
 * The hugetlb-vs-base decision is made by probing the first page rather than by
 * length/alignment alone: a large anonymous (base-page) mapping can happen to
 * be 2 MiB-aligned, and must not be mistaken for a hugetlb buffer.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_map_user_buf_to_sgl(struct slash_qdma_io_cb *iocb,
                                          bool write)
{
    unsigned long addr = (unsigned long)iocb->buf;
    size_t len = iocb->len;
    bool huge = false;

    iocb->pages_nr = 0;

    if (!addr || !len || addr > ULONG_MAX - len)
        return -EINVAL;

    if (!IS_ALIGNED(addr, PAGE_SIZE) || (len % PAGE_SIZE) != 0) {
        pr_err("slash: qdma: unsupported user transfer addr=0x%lx len=%zu (must be page-aligned and a multiple of 4 KiB)\n",
               addr, len);
        return -EINVAL;
    }

    /*
     * Only a 2 MiB-aligned, 2 MiB-multiple span can be hugetlb-backed.  Probe
     * the first page to confirm it actually is a hugetlb page before committing
     * to the huge path; otherwise fall through to the base-page path.
     */
    if (IS_ALIGNED(addr, SLASH_QDMA_HUGEPAGE_SIZE) &&
        (len % SLASH_QDMA_HUGEPAGE_SIZE) == 0) {
        struct page *probe = NULL;
        int probe_ret;

        probe_ret = get_user_pages_fast(addr, 1, 1 /* write */, &probe);
        if (probe_ret < 0)
            return probe_ret;
        if (probe_ret == 0)
            return -EFAULT;
        if (probe_ret == 1) {
            huge = slash_qdma_page_is_2m_hugetlb_head(probe);
            put_page(probe);
        }
    }

    if (huge)
        return slash_qdma_map_user_huge_page_to_sgl(iocb, write);

    return slash_qdma_map_user_base_pages_to_sgl(iocb, write);
}

/* ─────────────────────────────────────────────────────────────────────
 * Registered buffers: persistent pin + DMA mapping
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_buf_dma_unmap() - Tear down the cached DMA mapping of a buffer.
 * @buf: Registered buffer whose SGL entries were DMA-mapped.
 *
 * Unmaps every SGL entry that carries a non-zero dma_addr and clears it.
 * Safe to call on a partially-mapped buffer (used on the registration
 * error path).
 */
static void slash_qdma_buf_dma_unmap(struct slash_qdma_buf *buf)
{
    struct device *dev = &buf->qdma_dev->pdev->dev;
    unsigned int i;

    if (!buf->iocb.sgl)
        return;

    for (i = 0; i < buf->iocb.pages_nr; i++) {
        struct qdma_sw_sg *sg = &buf->iocb.sgl[i];

        if (sg->dma_addr) {
            dma_unmap_page(dev, sg->dma_addr, sg->len, DMA_BIDIRECTIONAL);
            sg->dma_addr = 0UL;
        }
    }
}

/**
 * slash_qdma_buf_dma_map() - DMA-map every SGL entry of a registered buffer.
 * @buf: Registered buffer with a freshly built (pinned) SGL.
 *
 * Maps each entry with DMA_BIDIRECTIONAL so the same cached mapping serves
 * both H2C and C2H transfers.  On any failure all previously mapped entries
 * are unmapped before returning.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_buf_dma_map(struct slash_qdma_buf *buf)
{
    struct device *dev = &buf->qdma_dev->pdev->dev;
    unsigned int i;

    for (i = 0; i < buf->iocb.pages_nr; i++) {
        struct qdma_sw_sg *sg = &buf->iocb.sgl[i];

        sg->dma_addr = dma_map_page(dev, sg->pg, sg->offset, sg->len,
                                    DMA_BIDIRECTIONAL);
        if (dma_mapping_error(dev, sg->dma_addr)) {
            sg->dma_addr = 0UL;
            pr_err("slash: qdma: buffer DMA map failed at entry %u/%u\n",
                   i, buf->iocb.pages_nr);
            slash_qdma_buf_dma_unmap(buf);
            return -ENOMEM;
        }
    }

    return 0;
}

/**
 * slash_qdma_buf_release() - kref release callback for a registered buffer.
 * @ref: kref embedded in the slash_qdma_buf being freed.
 *
 * Runs when the last reference drops (table ref plus any in-flight transfer
 * refs).  Tears down the DMA mapping, unpins the pages (marking them dirty in
 * case a C2H transfer wrote into them), frees the SGL, and frees the struct.
 */
static void slash_qdma_buf_release(struct kref *ref)
{
    struct slash_qdma_buf *buf =
        container_of(ref, struct slash_qdma_buf, ref);

    slash_qdma_buf_dma_unmap(buf);
    /* write=false marks the pages dirty: a C2H transfer may have written. */
    slash_qdma_unmap_user_buf(&buf->iocb, false);
    slash_qdma_iocb_release(&buf->iocb);
    kfree(buf);
}

static inline void slash_qdma_buf_get(struct slash_qdma_buf *buf)
{
    kref_get(&buf->ref);
}

static void slash_qdma_buf_put(struct slash_qdma_buf *buf)
{
    kref_put(&buf->ref, slash_qdma_buf_release);
}

/**
 * slash_qdma_client_release() - kref release callback for a control-fd client.
 * @ref: kref embedded in the slash_qdma_client being freed.
 *
 * Runs when the control fd and all qpair fds derived from it have closed.
 * By this point the buffer table has already been drained in
 * slash_qdma_fop_release(); here we just release the device reference and
 * free the client.
 */
static void slash_qdma_client_release(struct kref *ref)
{
    struct slash_qdma_client *client =
        container_of(ref, struct slash_qdma_client, ref);

    xa_destroy(&client->buffers);
    if (client->qdma_dev)
        kref_put(&client->qdma_dev->ref, slash_qdma_dev_release);
    kfree(client);
}

/**
 * slash_qdma_buf_lookup_get() - Look up a buffer by id and take a ref.
 * @client: Owning client context.
 * @buf_id: Buffer handle.
 *
 * Returns the buffer with an extra reference held, or NULL if no such
 * buffer exists.  The xa_lock serialises against unregister/teardown so the
 * buffer cannot be freed between lookup and the kref_get.
 */
static struct slash_qdma_buf *
slash_qdma_buf_lookup_get(struct slash_qdma_client *client, u32 buf_id)
{
    struct slash_qdma_buf *buf;

    xa_lock(&client->buffers);
    buf = xa_load(&client->buffers, buf_id);
    if (buf)
        slash_qdma_buf_get(buf);
    xa_unlock(&client->buffers);

    return buf;
}

/* ─────────────────────────────────────────────────────────────────────
 * IOCTL: buffer register / unregister
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_ioctl_buf_register_w() - Pin and DMA-map a host buffer.
 * @misc:   Miscdevice handle (for logging).
 * @client: Owning control-fd client.
 * @uarg:   User pointer to a struct slash_qdma_buf_register.
 *
 * Pins the pages backing the user buffer, builds a scatter-gather list
 * (reusing the same 4 KiB / 2 MiB granule detection as the per-transfer
 * path), DMA-maps every entry once, and inserts the resulting buffer into
 * the client's table under a freshly allocated buf_id.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_buf_register_w(struct miscdevice *misc,
                                           struct slash_qdma_client *client,
                                           void __user *uarg)
{
    struct slash_qdma_buf_register req;
    struct slash_qdma_dev *qdma_dev = client->qdma_dev;
    struct slash_qdma_buf *buf;
    __u32 user_size = 0;
    size_t copy_size;
    u32 buf_id;
    int rv;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_BUF_REGISTER_MIN_SIZE) {
        dev_warn(misc->this_device,
                 "qdma: BUF_REGISTER size too small (%u)\n", user_size);
        return -EINVAL;
    }

    memset(&req, 0, sizeof(req));
    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    if (req.flags != 0)
        return -EINVAL;

    if (req.length == 0 || (req.length % PAGE_SIZE) != 0)
        return -EINVAL;

    if ((req.user_addr % PAGE_SIZE) != 0)
        return -EINVAL;

    buf = kzalloc(sizeof(*buf), GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    kref_init(&buf->ref);
    buf->qdma_dev = qdma_dev;
    buf->length = req.length;
    buf->iocb.buf = (void __user *)(unsigned long)req.user_addr;
    buf->iocb.len = (size_t)req.length;

    /*
     * Pin the pages and build the SGL once.  Pin writable (write=true) so
     * the same registration serves C2H transfers, where the device writes
     * into the pages.
     */
    rv = slash_qdma_map_user_buf_to_sgl(&buf->iocb, true);
    if (rv < 0) {
        kfree(buf);
        return rv;
    }

    if (buf->iocb.pages_nr == 0 || !buf->iocb.sgl) {
        slash_qdma_unmap_user_buf(&buf->iocb, false);
        slash_qdma_iocb_release(&buf->iocb);
        kfree(buf);
        return -EINVAL;
    }

    buf->granule = buf->iocb.sgl[0].len;

    rv = slash_qdma_buf_dma_map(buf);
    if (rv < 0) {
        slash_qdma_unmap_user_buf(&buf->iocb, false);
        slash_qdma_iocb_release(&buf->iocb);
        kfree(buf);
        return rv;
    }

    rv = xa_alloc(&client->buffers, &buf_id, buf,
                  SLASH_QDMA_BUF_ID_RANGE, GFP_KERNEL);
    if (rv < 0) {
        slash_qdma_buf_dma_unmap(buf);
        slash_qdma_unmap_user_buf(&buf->iocb, false);
        slash_qdma_iocb_release(&buf->iocb);
        kfree(buf);
        return rv;
    }

    buf->buf_id = buf_id;

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "buf register: id=%u addr=0x%llx len=%llu granule=%llu entries=%u\n",
                          buf_id, (unsigned long long)req.user_addr,
                          (unsigned long long)req.length,
                          (unsigned long long)buf->granule,
                          buf->iocb.pages_nr);

    /* Copy the assigned buf_id back to userspace. */
    req.size = sizeof(req);
    req.buf_id = buf_id;
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size)) {
        xa_erase(&client->buffers, buf_id);
        slash_qdma_buf_put(buf);
        return -EFAULT;
    }
    if (user_size > sizeof(req)) {
        if (clear_user((void __user *)((unsigned long)uarg + sizeof(req)),
                       user_size - sizeof(req))) {
            xa_erase(&client->buffers, buf_id);
            slash_qdma_buf_put(buf);
            return -EFAULT;
        }
    }

    return 0;
}

/**
 * slash_qdma_ioctl_buf_unregister_w() - Drop a registered buffer.
 * @misc:   Miscdevice handle (unused).
 * @client: Owning control-fd client.
 * @uarg:   User pointer to a struct slash_qdma_buf_unregister.
 *
 * Removes the buffer from the client table so no new transfer can find it,
 * then drops the table's reference.  Actual unpin/unmap is deferred to the
 * buffer's release callback once any in-flight transfer has finished.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int slash_qdma_ioctl_buf_unregister_w(struct miscdevice *misc,
                                             struct slash_qdma_client *client,
                                             void __user *uarg)
{
    struct slash_qdma_buf_unregister req;
    struct slash_qdma_buf *buf;
    __u32 user_size = 0;

    (void)misc;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_BUF_UNREGISTER_MIN_SIZE)
        return -EINVAL;

    memset(&req, 0, sizeof(req));
    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    buf = xa_erase(&client->buffers, req.buf_id);
    if (!buf)
        return -ENOENT;

    slash_qdma_buf_put(buf);

    return 0;
}

/**
 * slash_qdma_qpair_read_write() - Perform a DMA transfer via a qpair fd.
 * @file:  The anon_inode file for this queue pair.
 * @buf:   User-space buffer (source for write/H2C, destination for read/C2H).
 * @count: Number of bytes to transfer.
 * @ppos:  File position — used as the device-side (endpoint) address.
 *         Updated on success to reflect the bytes transferred, enabling
 *         sequential positional I/O.
 * @write: true for H2C (host-to-card write), false for C2H (card-to-host read).
 *
 * Transfer flow:
 *   1. Validate context and check that the required direction (H2C or C2H)
 *      is enabled on this queue pair.
 *   2. Pin user pages and build a scatter-gather list.
 *   3. Populate a qdma_request:
 *      - ep_addr = *ppos: the device-side address (FPGA memory offset).
 *      - h2c_eot = 1: signals end-of-transfer to the FPGA, allowing it to
 *        process the complete data packet.
 *      - timeout_ms = 10000 (10 seconds): if the transfer doesn't complete
 *        in this time, qdma_request_submit returns an error.
 *      - fp_done = NULL: synchronous mode — the call blocks until completion.
 *        If fp_done were set, libqdma would call it asynchronously.
 *      - dma_mapped = 0: libqdma handles the DMA mapping internally.
 *   4. Submit to libqdma via qdma_request_submit().
 *   5. On success, advance *ppos by the number of bytes transferred.
 *   6. Unpin pages and free the SGL.
 *
 * Return: Number of bytes transferred (>= 0) on success, negative errno
 *         on failure.
 */
static ssize_t slash_qdma_qpair_read_write(struct file *file, char __user *buf,
                                           size_t count, loff_t *ppos,
                                           bool write)
{
    struct slash_qdma_qpair_file_ctx *ctx = file->private_data;
    struct slash_qdma_dev *qdma_dev;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_io_cb iocb;
    struct qdma_request *req;
    unsigned long qhndl;
    ssize_t res;
    int rv;
#if SLASH_QDMA_TIMING
    ktime_t t_start, t_mapped, t_submitted, t_done;
#endif

    if (!ctx)
        return -EINVAL;

    qdma_dev = ctx->qdma_dev;
    entry = ctx->entry;

    if (!qdma_dev || !entry)
        return -ENODEV;

    /* Check device liveness and resolve the queue handle for the direction. */
    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }

    if (write) {
        /* H2C: writing data from host to card */
        if (!(entry->dir_mask & SLASH_QDMA_DIR_H2C) ||
            !slash_qdma_qhndl_is_valid(entry->qhndl[Q_H2C])) {
            mutex_unlock(&qdma_dev->lock);
            return -ENODEV;
        }
        qhndl = entry->qhndl[Q_H2C];
    } else {
        /* C2H: reading data from card to host */
        if (!(entry->dir_mask & SLASH_QDMA_DIR_C2H) ||
            !slash_qdma_qhndl_is_valid(entry->qhndl[Q_C2H])) {
            mutex_unlock(&qdma_dev->lock);
            return -ENODEV;
        }
        qhndl = entry->qhndl[Q_C2H];
    }
    mutex_unlock(&qdma_dev->lock);

    /* Pin user pages and build the scatter-gather list. */
#if SLASH_QDMA_TIMING
    t_start = ktime_get();
#endif
    memset(&iocb, 0, sizeof(iocb));
    iocb.buf = buf;
    iocb.len = count;
    rv = slash_qdma_map_user_buf_to_sgl(&iocb, write);
    if (rv < 0)
        return rv;
#if SLASH_QDMA_TIMING
    t_mapped = ktime_get();
#endif

    /* Populate the libqdma request structure. */
    req = &iocb.req;
    req->sgcnt = iocb.pages_nr;         /* Number of SGL entries */
    req->sgl = iocb.sgl;                /* Scatter-gather list */
    req->write = write ? 1 : 0;         /* Direction flag for libqdma */
    req->dma_mapped = 0;                /* Let libqdma handle DMA mapping */
    req->udd_len = 0;                   /* No user-defined data */
    req->ep_addr = (u64)*ppos;           /* Device-side (endpoint) address */
    req->count = count;                  /* Total byte count */
    req->timeout_ms = 10 * 1000;         /* 10-second timeout */
    req->fp_done = NULL;                 /* Synchronous: block until complete */
    req->h2c_eot = 1;                   /* End-of-transfer marker for FPGA */

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "qdma_request_submit start: qid=%u qhndl=%lu write=%d count=%zu ep_addr=0x%llx\n",
                          ctx->qid, qhndl, req->write, req->count,
                          (unsigned long long)req->ep_addr);
    res = qdma_request_submit(qdma_dev->qdma_handle, qhndl, req);
    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "qdma_request_submit done: qid=%u qhndl=%lu res=%zd\n",
                          ctx->qid, qhndl, res);
#if SLASH_QDMA_TIMING
    t_submitted = ktime_get();
#endif

    /* Advance the file position by the number of bytes transferred. */
    if (res > 0)
        *ppos += res;

    /* Unpin pages (marking dirty for C2H reads) and free the SGL. */
    slash_qdma_unmap_user_buf(&iocb, write);
    slash_qdma_iocb_release(&iocb);

#if SLASH_QDMA_TIMING
    t_done = ktime_get();
    dev_info(&qdma_dev->pdev->dev,
             "slash: qdma: timing qid=%u %s count=%zu sgcnt=%u ep=0x%llx res=%zd | map=%lld submit=%lld unmap=%lld total=%lld ns\n",
             ctx->qid, write ? "H2C" : "C2H", count, req->sgcnt,
             (unsigned long long)req->ep_addr, res,
             ktime_to_ns(ktime_sub(t_mapped, t_start)),
             ktime_to_ns(ktime_sub(t_submitted, t_mapped)),
             ktime_to_ns(ktime_sub(t_done, t_submitted)),
             ktime_to_ns(ktime_sub(t_done, t_start)));
#endif

    return res;
}

/**
 * slash_qdma_qpair_read() - Read (C2H) file operation for a qpair fd.
 * @file:  Anon_inode file for the queue pair.
 * @buf:   User-space destination buffer.
 * @count: Number of bytes to read.
 * @ppos:  Device-side address to read from.
 *
 * Thin wrapper that delegates to slash_qdma_qpair_read_write() with
 * write=false (C2H direction).
 *
 * Return: Bytes transferred or negative errno.
 */
static ssize_t slash_qdma_qpair_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos)
{
    return slash_qdma_qpair_read_write(file, buf, count, ppos, false);
}

/**
 * slash_qdma_qpair_write() - Write (H2C) file operation for a qpair fd.
 * @file:  Anon_inode file for the queue pair.
 * @buf:   User-space source buffer.
 * @count: Number of bytes to write.
 * @ppos:  Device-side address to write to.
 *
 * Thin wrapper that delegates to slash_qdma_qpair_read_write() with
 * write=true (H2C direction).
 *
 * Return: Bytes transferred or negative errno.
 */
static ssize_t slash_qdma_qpair_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos)
{
    return slash_qdma_qpair_read_write(file, (char __user *)buf,
                                       count, ppos, true);
}

/**
 * slash_qdma_qpair_transfer() - Registered-buffer DMA transfer on a qpair fd.
 * @file: Anon_inode file for the queue pair.
 * @uarg: User pointer to a struct slash_qdma_transfer.
 *
 * Looks up the registered buffer by id in the owning client, validates the
 * requested slice against the buffer's page granule and length, resolves the
 * queue handle for the requested direction, and submits the cached,
 * pre-DMA-mapped SGL slice (req->dma_mapped = 1) to libqdma.
 *
 * Unlike the legacy read/write path, no pages are pinned or DMA-mapped here:
 * that work was amortised at registration time.
 *
 * Return: number of bytes transferred (>= 0) on success, negative errno on
 *         failure.
 */
static long slash_qdma_qpair_transfer(struct file *file, void __user *uarg)
{
    struct slash_qdma_qpair_file_ctx *ctx = file->private_data;
    struct slash_qdma_transfer req;
    struct slash_qdma_dev *qdma_dev;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_client *client;
    struct slash_qdma_buf *buf;
    struct qdma_request qreq;
    unsigned long qhndl;
    bool write;
    u32 dir_bit;
    enum queue_type_t qtype;
    u64 start_entry, n_entries;
    __u32 user_size = 0;
    ssize_t res;

    if (!ctx)
        return -EINVAL;

    qdma_dev = ctx->qdma_dev;
    entry = ctx->entry;
    client = ctx->client;

    if (!qdma_dev || !entry || !client)
        return -ENODEV;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_TRANSFER_MIN_SIZE)
        return -EINVAL;

    memset(&req, 0, sizeof(req));
    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    switch (req.direction) {
    case SLASH_QDMA_XFER_H2C:
        write = true;
        dir_bit = SLASH_QDMA_DIR_H2C;
        qtype = Q_H2C;
        break;
    case SLASH_QDMA_XFER_C2H:
        write = false;
        dir_bit = SLASH_QDMA_DIR_C2H;
        qtype = Q_C2H;
        break;
    default:
        return -EINVAL;
    }

    /* Resolve and ref the registered buffer. */
    buf = slash_qdma_buf_lookup_get(client, req.buf_id);
    if (!buf)
        return -ENOENT;

    /* Validate the requested slice against the buffer's page granule. */
    if (buf->granule == 0 || req.length == 0 ||
        (req.buf_offset % buf->granule) != 0 ||
        (req.length % buf->granule) != 0) {
        slash_qdma_buf_put(buf);
        return -EINVAL;
    }
    if (req.buf_offset > buf->length ||
        req.length > buf->length - req.buf_offset) {
        slash_qdma_buf_put(buf);
        return -EINVAL;
    }

    start_entry = req.buf_offset / buf->granule;
    n_entries = req.length / buf->granule;
    if (start_entry + n_entries > buf->iocb.pages_nr) {
        slash_qdma_buf_put(buf);
        return -EINVAL;
    }

    /* Check device liveness and resolve the queue handle for the direction. */
    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        slash_qdma_buf_put(buf);
        return -ENODEV;
    }
    if (!(entry->dir_mask & dir_bit) ||
        !slash_qdma_qhndl_is_valid(entry->qhndl[qtype])) {
        mutex_unlock(&qdma_dev->lock);
        slash_qdma_buf_put(buf);
        return -ENODEV;
    }
    qhndl = entry->qhndl[qtype];
    mutex_unlock(&qdma_dev->lock);

    /*
     * Submit the cached SGL slice.  dma_mapped = 1 tells libqdma the SGL is
     * already DMA-mapped (dma_addr filled at registration), so it skips the
     * per-request map/unmap entirely.
     */
    memset(&qreq, 0, sizeof(qreq));
    qreq.sgcnt = (unsigned int)n_entries;
    qreq.sgl = &buf->iocb.sgl[start_entry];
    qreq.write = write ? 1 : 0;
    qreq.dma_mapped = 1;
    qreq.udd_len = 0;
    qreq.ep_addr = (u64)req.dev_addr;
    qreq.count = (unsigned int)req.length;
    qreq.timeout_ms = 10 * 1000;
    qreq.fp_done = NULL;
    qreq.h2c_eot = 1;

    SLASH_QDMA_OP_DEV_LOG(&qdma_dev->pdev->dev,
                          "transfer: qid=%u buf=%u off=%llu dev=0x%llx len=%llu dir=%s\n",
                          ctx->qid, req.buf_id,
                          (unsigned long long)req.buf_offset,
                          (unsigned long long)req.dev_addr,
                          (unsigned long long)req.length,
                          write ? "H2C" : "C2H");

    res = qdma_request_submit(qdma_dev->qdma_handle, qhndl, &qreq);

    slash_qdma_buf_put(buf);

    if (res < 0)
        return (long)res;

    return (long)res;
}

/**
 * slash_qdma_qpair_ioctl() - Ioctl handler for per-qpair anon_inode fds.
 * @file: Anon_inode file.
 * @cmd:  Ioctl command number.
 * @arg:  User-space argument.
 *
 * Supports SLASH_QDMA_QPAIR_IOCTL_TRANSFER (registered-buffer DMA transfer).
 *
 * Return: bytes transferred (>= 0) for TRANSFER, or -ENOTTY for any other
 *         command.
 */
static long slash_qdma_qpair_ioctl(struct file *file,
                                   unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case SLASH_QDMA_QPAIR_IOCTL_TRANSFER:
        return slash_qdma_qpair_transfer(file, (void __user *)arg);
    default:
        return -ENOTTY;
    }
}

/**
 * slash_qdma_qpair_release() - Release handler for per-qpair anon_inode fds.
 * @inode: Inode (unused for anon_inodes).
 * @file:  The file being closed.
 *
 * Drops the references acquired in slash_qdma_ioctl_qpair_get_fd_w():
 *   - One ref on the qpair entry (may free the entry if the qpair has
 *     already been deleted from the xarray).
 *   - One ref on the QDMA device (may free the device if it has already
 *     been removed from PCI).
 *
 * Also frees the file context structure.
 *
 * Return: Always 0.
 */
static int slash_qdma_qpair_release(struct inode *inode, struct file *file)
{
    struct slash_qdma_qpair_file_ctx *ctx = file->private_data;

    (void)inode;

    if (ctx) {
        if (ctx->entry)
            slash_qdma_qpair_put(ctx->entry);
        if (ctx->client)
            kref_put(&ctx->client->ref, slash_qdma_client_release);
        if (ctx->qdma_dev)
            kref_put(&ctx->qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        file->private_data = NULL;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────
 * IOCTL: qpair get fd
 * ───────────────────────────────────────────────────────────────────── */

/**
 * slash_qdma_ioctl_qpair_get_fd_w() - Create an anon_inode fd for a queue pair.
 * @misc:     Miscdevice handle (unused).
 * @qdma_dev: QDMA device.
 * @uarg:     User-space pointer to a slash_qdma_qpair_fd_request struct.
 *
 * Creates an anonymous inode file descriptor that userspace can use
 * for read() (C2H) and write() (H2C) DMA transfers on the specified
 * queue pair.  The fd holds references to both the qpair entry and the
 * device, preventing either from being freed while the fd is open.
 *
 * The only supported flag is O_CLOEXEC (close-on-exec).
 *
 * The file is created with FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE
 * enabled, allowing pread/pwrite and lseek to set the device-side
 * address for DMA transfers.
 *
 * Error handling: on any failure after resources are acquired, all
 * refs and allocations are cleaned up before returning.
 *
 * Return: The new fd (>= 0) on success, negative errno on failure.
 */
static int slash_qdma_ioctl_qpair_get_fd_w(struct miscdevice *misc,
                                           struct slash_qdma_client *client,
                                           void __user *uarg)
{
    struct slash_qdma_dev *qdma_dev = client->qdma_dev;
    struct slash_qdma_qpair_fd_request req;
    __u32 user_size = 0;
    size_t copy_size;
    struct slash_qdma_qpair_entry *entry;
    struct slash_qdma_qpair_file_ctx *ctx;
    struct file *file;
    int fd;
    int err;

    if (copy_from_user(&user_size, uarg, sizeof(user_size)))
        return -EFAULT;

    if (user_size < SLASH_QDMA_QPAIR_GET_FD_MIN_SIZE) {
        dev_warn(misc->this_device,
                 "qdma: QPAIR_GET_FD size too small (%u)\n", user_size);
        return -EINVAL;
    }

    memset(&req, 0, sizeof(req));

    if (copy_from_user(&req, uarg, min_t(size_t, user_size, sizeof(req))))
        return -EFAULT;

    /* Only O_CLOEXEC is a valid flag. */
    if (req.flags & ~O_CLOEXEC)
        return -EINVAL;

    /* Look up the qpair entry and take refs while holding the lock. */
    mutex_lock(&qdma_dev->lock);
    if (qdma_dev->hw_shutdown || !qdma_dev->have_qdma_handle) {
        mutex_unlock(&qdma_dev->lock);
        return -ENODEV;
    }

    entry = slash_qdma_qpair_lookup(qdma_dev, req.qid);
    if (!entry || !entry->dir_mask) {
        mutex_unlock(&qdma_dev->lock);
        return -ENOENT;
    }

    /*
     * Take a ref on the entry and the device.  These refs are held by
     * the file context and released when the fd is closed, ensuring
     * neither the entry nor the device can be freed prematurely.
     */
    slash_qdma_qpair_get(entry);
    kref_get(&qdma_dev->ref);
    /*
     * Take a ref on the owning client so handle-based transfers issued on
     * this qpair fd can resolve registered buffers even if the control fd
     * that created the qpair is closed first.
     */
    kref_get(&client->ref);
    mutex_unlock(&qdma_dev->lock);

    /* Allocate the per-fd context. */
    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        slash_qdma_qpair_put(entry);
        kref_put(&client->ref, slash_qdma_client_release);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        return -ENOMEM;
    }

    ctx->qdma_dev = qdma_dev;
    ctx->entry = entry;
    ctx->client = client;
    ctx->qid = req.qid;

    /* Create the anonymous inode file with read/write access. */
    file = anon_inode_getfile("slash_qdma_qpair", &slash_qdma_qpair_fops,
                              ctx, O_RDWR | (req.flags & O_CLOEXEC));
    if (IS_ERR(file)) {
        err = PTR_ERR(file);
        slash_qdma_qpair_put(entry);
        kref_put(&client->ref, slash_qdma_client_release);
        kref_put(&qdma_dev->ref, slash_qdma_dev_release);
        kfree(ctx);
        return err;
    }

    /* Enable seek and positional read/write for device-address control. */
    file->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;


    /* Allocate a file descriptor number. */
    fd = get_unused_fd_flags(req.flags & O_CLOEXEC);
    if (fd < 0) {
        fput(file); /* triggers slash_qdma_qpair_release -> drops entry/dev refs, frees ctx */
        return fd;
    }

    /* Copy the response back to userspace before installing the fd. */
    req.size = sizeof(req);
    copy_size = min_t(size_t, user_size, sizeof(req));
    if (copy_to_user(uarg, &req, copy_size)) {
        put_unused_fd(fd);
        fput(file); /* triggers slash_qdma_qpair_release -> drops entry/dev refs, frees ctx */
        return -EFAULT;
    }
    if (user_size > sizeof(req)) {
        if (clear_user((void __user *)((unsigned long)uarg + sizeof(req)),
                       user_size - sizeof(req))) {
            put_unused_fd(fd);
            fput(file);
            return -EFAULT;
        }
    }

    /*
     * Install the fd.  After this point the fd is visible to userspace
     * and the file's release callback will handle cleanup.
     */
    fd_install(fd, file);

    return fd;
}
