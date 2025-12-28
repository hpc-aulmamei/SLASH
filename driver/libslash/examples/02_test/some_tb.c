#include <stdio.h>

#include <slash/ctldev.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/* ---------- Constants / Registers ---------- */

#define NUM_CORES        64
#define ITERATIONS       10  /* HLS iterations per core */

#define REG_AP_CTRL      0x00
#define REG_HBM_PTR_L    0x10
#define REG_HBM_PTR_H    0x14
#define REG_MODE         0x1C
#define REG_LENGTH       0x24
#define REG_ITERATIONS   0x2C

#define MODE_WRITE       0
#define LENGTH_WORDS     0x800000U   /* 8M * 64B = 512MB */
#define AP_DONE_VALUE    0x04U
#define UNMAPPED_SENTINEL 0xFFFFFFFFU

#define BAR_SIZE_BYTES   (128U * 1024U * 1024U)
#define TIMEOUT_SECONDS  100
#define POLL_NS          1000000L    /* 1 ms */

/* MMIO offsets per core (base of each core's register window) */
static const unsigned int MMIO_OFFSETS[NUM_CORES] = {
    0x000000, 0x010000, 0x0C0000, 0x170000,
    0x220000, 0x2D0000, 0x380000, 0x3D0000,
    0x3E0000, 0x3F0000, 0x020000, 0x030000,
    0x040000, 0x050000, 0x060000, 0x070000,
    0x080000, 0x090000, 0x0A0000, 0x0B0000,
    0x0D0000, 0x0E0000, 0x0F0000, 0x100000,
    0x110000, 0x120000, 0x130000, 0x140000,
    0x150000, 0x160000, 0x180000, 0x190000,
    0x1A0000, 0x1B0000, 0x1C0000, 0x1D0000,
    0x1E0000, 0x1F0000, 0x200000, 0x210000,
    0x230000, 0x240000, 0x250000, 0x260000,
    0x270000, 0x280000, 0x290000, 0x2A0000,
    0x2B0000, 0x2C0000, 0x2E0000, 0x2F0000,
    0x300000, 0x310000, 0x320000, 0x330000,
    0x340000, 0x350000, 0x360000, 0x370000,
    0x390000, 0x3A0000, 0x3B0000, 0x3C0000
};

/* ---------- Low-level MMIO helpers (unsafe: must be bracketed) ---------- */

static __inline__ void mmio_write32_unsafe(struct slash_bar_file *bar, unsigned int off, unsigned int val)
{
    volatile unsigned int *p;
    p = (volatile unsigned int *)((char *)bar->map + off);
    *p = val;
}

static __inline__ unsigned int mmio_read32_unsafe(struct slash_bar_file *bar, unsigned int off)
{
    volatile unsigned int *p;
    p = (volatile unsigned int *)((char *)bar->map + off);
    return *p;
}

/* ---------- HBM address computation without 64-bit types ---------- */
/*
 * HBM base address = 0x00000004_00000000
 * Per-core stride   = 0x00000000_20000000 (512MB)
 * For core in [0..63], split into HI/LO 32-bit parts:
 *   carry  = core >> 3
 *   lo     = (core & 0x7) << 29
 *   hi     = 0x00000004 + carry
 */
static void compute_hbm_addr_parts(unsigned int core,
                                   unsigned int *lo32, unsigned int *hi32)
{
    unsigned int carry = (core >> 3) & 0xFFFFFFFFU;
    unsigned int lo    = (core & 0x7U) << 29;
    unsigned int hi    = 0x00000004U + carry;

    *lo32 = lo;
    *hi32 = hi;
}

/* ---------- Utility: timespec difference in seconds (double) ---------- */

static double timespec_diff_sec(const struct timespec *a, const struct timespec *b)
{
    /* returns a - b */
    double s = (double)a->tv_sec - (double)b->tv_sec;
    double n = (double)a->tv_nsec - (double)b->tv_nsec;
    return s + n * 1e-9;
}

/* ---------- Core routines ---------- */

static int prepare_all_cores(struct slash_bar_file *bar, unsigned int iterations)
{
    int rc;
    unsigned int i;

    rc = slash_bar_file_start_write(bar);
    if (rc < 0) {
        fprintf(stderr, "start_write failed: %s\n", strerror(errno));
        return -1;
    }

    for (i = 0; i < NUM_CORES; ++i) {
        unsigned int base = MMIO_OFFSETS[i];
        unsigned int lo32, hi32;

        compute_hbm_addr_parts(i, &lo32, &hi32);

        mmio_write32_unsafe(bar, base + REG_HBM_PTR_L, lo32);
        mmio_write32_unsafe(bar, base + REG_HBM_PTR_H, hi32);
        mmio_write32_unsafe(bar, base + REG_MODE,       MODE_WRITE);
        mmio_write32_unsafe(bar, base + REG_LENGTH,     LENGTH_WORDS);
        mmio_write32_unsafe(bar, base + REG_ITERATIONS, iterations);
    }

    rc = slash_bar_file_end_write(bar);
    if (rc < 0) {
        fprintf(stderr, "end_write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int start_all_cores(struct slash_bar_file *bar)
{
    int rc;
    unsigned int i;

    rc = slash_bar_file_start_write(bar);
    if (rc < 0) {
        fprintf(stderr, "start_write failed: %s\n", strerror(errno));
        return -1;
    }

    for (i = 0; i < NUM_CORES; ++i) {
        unsigned int base = MMIO_OFFSETS[i];
        mmio_write32_unsafe(bar, base + REG_AP_CTRL, 0x1U);
    }

    rc = slash_bar_file_end_write(bar);
    if (rc < 0) {
        fprintf(stderr, "end_write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Polls all cores until done or timeout. Returns 0 on success, -1 on failure. */
static int wait_all_done(struct slash_bar_file *bar, double timeout_sec,
                         int *failed_out, unsigned int *num_failed_out)
{
    int rc;
    unsigned char done[NUM_CORES];
    unsigned int remaining = NUM_CORES;
    unsigned int i;
    struct timespec t_start, t_now;
    struct timespec req;

    for (i = 0; i < NUM_CORES; ++i) done[i] = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &t_start) != 0) {
        fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return -1;
    }

    req.tv_sec = 0;
    req.tv_nsec = POLL_NS;

    while (1) {
        rc = slash_bar_file_start_read(bar);
        if (rc < 0) {
            fprintf(stderr, "start_read failed: %s\n", strerror(errno));
            return -1;
        }

        for (i = 0; i < NUM_CORES; ++i) {
            unsigned int base, v;

            if (done[i]) continue;

            base = MMIO_OFFSETS[i];
            v = mmio_read32_unsafe(bar, base + REG_AP_CTRL);

            if (v == UNMAPPED_SENTINEL) {
                /* Treat as failure for this core */
                done[i] = 1;
                if (failed_out) failed_out[*num_failed_out] = (int)i;
                if (num_failed_out) (*num_failed_out)++;
                if (remaining > 0) remaining--;
                fprintf(stderr, "[!] Core %u BAR region not mapped (read 0xFFFFFFFF)\n", i);
            } else if (v == AP_DONE_VALUE) {
                done[i] = 1;
                if (remaining > 0) remaining--;
            } else {
                /* still running */
            }
        }

        rc = slash_bar_file_end_read(bar);
        if (rc < 0) {
            fprintf(stderr, "end_read failed: %s\n", strerror(errno));
            return -1;
        }

        if (remaining == 0) break;

        if (clock_gettime(CLOCK_MONOTONIC, &t_now) != 0) {
            fprintf(stderr, "clock_gettime() failed: %s\n", strerror(errno));
            return -1;
        }
        if (timespec_diff_sec(&t_now, &t_start) >= timeout_sec) {
            /* Timeout: mark any still-running cores as failed */
            for (i = 0; i < NUM_CORES; ++i) {
                if (!done[i]) {
                    if (failed_out) failed_out[*num_failed_out] = (int)i;
                    if (num_failed_out) (*num_failed_out)++;
                }
            }
            fprintf(stderr, "[!] Timeout waiting for cores\n");
            return -1;
        }

        /* 1 ms sleep between polls */
        nanosleep(&req, (struct timespec *)0);
    }

    return 0;
}

/* ---------- Public entry: run the testbench on an already-mapped BAR ---------- */

int run_testbench(struct slash_bar_file *bar)
{
    int rc;
    struct timespec t0, t1;
    int failed_idx[NUM_CORES];
    unsigned int num_failed = 0;
    double duration_s, total_bytes, throughput_gib_s;

    if (!bar || !bar->map) {
        fprintf(stderr, "Invalid BAR mapping\n");
        return -1;
    }
    if (bar->len < BAR_SIZE_BYTES) {
        /* Not strictly required if your design only uses the listed offsets,
           but keep the check to mirror the Python's BAR_SIZE intent. */
        /* Not fatal; warn only. */
        fprintf(stderr, "[!] Warning: BAR length (%lu) < expected (%u)\n",
                (unsigned long)bar->len, (unsigned int)BAR_SIZE_BYTES);
    }

    /* Prepare cores */
    if (prepare_all_cores(bar, (unsigned int)ITERATIONS) < 0) {
        return -1;
    }

    /* Start timing before asserting start, like the Python version */
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
        fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return -1;
    }

    /* Kick all cores */
    rc = start_all_cores(bar);
    if (rc < 0) return -1;

    /* Wait for completion (single-threaded polling) */
    num_failed = 0;
    rc = wait_all_done(bar, (double)TIMEOUT_SECONDS, failed_idx, &num_failed);

    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return -1;
    }

    duration_s = timespec_diff_sec(&t1, &t0);

    if (rc < 0) {
        unsigned int i;
        if (num_failed > 0) {
            fprintf(stderr, "[!] Cores failed: ");
            for (i = 0; i < num_failed; ++i) {
                fprintf(stderr, "%d%s", failed_idx[i], (i + 1 < num_failed) ? ", " : "");
            }
            fprintf(stderr, "\n");
        }
        return -1;
    }

    /* Success: compute throughput (GiB/s) */
    total_bytes = (double)NUM_CORES * (double)ITERATIONS * (512.0 * 1024.0 * 1024.0); /* 512MB per iter per core */
    throughput_gib_s = total_bytes / duration_s / (1024.0 * 1024.0 * 1024.0);

    printf("[\xE2\x9C\x93] All cores done in %.3f ms (%.2f GiB/s)\n",
           duration_s * 1000.0, throughput_gib_s);

    return 0;
}

int main()
{
    struct slash_bar_file *bar_file;
    struct slash_ctldev *ctldev;

    ctldev = slash_ctldev_open("/dev/slash_ctl0");
    if (ctldev == NULL) {
        perror("1");
        return 1;
    }

    bar_file = slash_bar_file_open(ctldev, 0, 0);
    if (bar_file == NULL) {
        perror("2");
        (void) slash_ctldev_close(ctldev);
        return 2;
    }

    run_testbench(bar_file);

    if (slash_bar_file_close(bar_file) != 0) {
        perror("3");
        (void) slash_ctldev_close(ctldev);
        return 3;
    }

    if (slash_ctldev_close(ctldev) != 0) {
        perror("4");
        return 4;
    }

    return 0;
}
