/*
 * RPU QDMA test ELF (R5_1, bare metal, loaded over JTAG in place of RP1).
 *
 * The R5 reaches the CPM5 AXI slave through the rpuq static (shell/patch_rpuq.py):
 *   R5 0x9000_0000 + n * 2 MiB  -> slave bridge window n (BDF entry n) -> host memory
 *   R5 0x9800_0000, 16 MiB      -> QDMA CSRs and queue space (CPM5 "MULTQ" decode)
 * and the CPM5 registers (BDF table at 0xFCEA_2420 + 0x20 * n) on the PS bus.
 * PG347: Device memory, 32-bit accesses only, for all of these.
 *
 * The host drives it through a mailbox in shared DDR: R5 0x3010_0000 =
 * PF2 BAR4 + 0x0410_0000 (static NoC remap 0x204_0400_0000 -> 0x3000_0000).
 * The host fills the parameters, then increments cmd_seq; the ELF runs the op
 * and sets ack_seq = cmd_seq when it is done (state DONE or ERROR).
 *
 * Ops:
 *   OP_PROBE  read up to 8 R5 addresses (32-bit), results in probe_val[]
 *   OP_H2C    post count descriptors on one ST H2C queue whose ring the
 *             driver filled (EXT_PRODUCER): only the PIDX doorbell is written.
 *             With FLAG_FLOW, CIDX comes from the queue's status writeback in
 *             host memory through BDF entry bdf_entry (set and cleared here);
 *             without it, at most rngsz - 1 descriptors, one doorbell.
 *   OP_BDF    write one BDF entry (pa, perm, prot) and read it back; pa 0 clears
 */
#include <stdint.h>

#define MBOX_ADDR        0x30100000u
#define MBOX_MAGIC       0x51555052u     /* "RPUQ" */
#define MBOX_VERSION     2u

#define R5_HOST_BASE     0x90000000u     /* slave bridge window 0 */
#define R5_HOST_WIN      0x00200000u     /* 2 MiB per BDF entry */
#define R5_QDMA_BASE     0x98000000u
#define R5_QDMA_SIZE     0x00800000u     /* the CPM decodes 8 MiB (qspc_csr_size 3); the remap is 16 MiB */
#define BDF_TABLE        0xFCEA2420u
#define BDF_STRIDE       0x20u

enum { ST_IDLE = 0, ST_RUNNING = 1, ST_DONE = 2, ST_ERROR = 3 };
enum { OP_PROBE = 1, OP_H2C = 2, OP_BDF = 3 };
enum { FLAG_FLOW = 1u << 0 };
enum {
    E_OK = 0, E_OP = 1, E_ADDR = 2, E_RING = 3, E_STALL = 4, E_BDF = 5,
    E_ABORT = 6,    /* data abort: fault address in last_cidx, DFSR in polls; ELF parked */
};

struct mbox {
    volatile uint32_t magic;          /* 0x00 ELF */
    volatile uint32_t version;        /* 0x04 ELF */
    volatile uint32_t state;          /* 0x08 ELF */
    volatile uint32_t cmd_seq;        /* 0x0C host */
    volatile uint32_t ack_seq;        /* 0x10 ELF */
    volatile uint32_t op;             /* 0x14 host */
    volatile uint32_t db_addr;        /* 0x18 host: R5 address of the doorbell */
    volatile uint32_t rngsz;          /* 0x1C host */
    volatile uint32_t count;          /* 0x20 host: descriptors to post */
    volatile uint32_t batch;          /* 0x24 host: descriptors per doorbell */
    volatile uint32_t flags;          /* 0x28 host */
    volatile uint32_t bdf_entry;      /* 0x2C host: 0..63 */
    volatile uint32_t pa_lo;          /* 0x30 host: wb (OP_H2C) or entry pa (OP_BDF) */
    volatile uint32_t pa_hi;          /* 0x34 host */
    volatile uint32_t bdf_prot;       /* 0x38 host */
    volatile uint32_t bdf_perm;       /* 0x3C host */
    volatile uint32_t start_pidx;     /* 0x40 host: hardware PIDX now (0 after START) */
    volatile uint32_t stall_limit;    /* 0x44 host: polls without progress before E_STALL */
    volatile uint32_t probe_fmask;    /* 0x48 ELF: OP_PROBE bit i = address i aborted */
    volatile uint32_t probe_dfsr[8];  /* 0x4C ELF: DFSR per aborted address */
    volatile uint32_t rsvd0[5];       /* 0x6C */
    volatile uint32_t err;            /* 0x80 ELF */
    volatile uint32_t posted;         /* 0x84 ELF */
    volatile uint32_t final_pidx;     /* 0x88 ELF */
    volatile uint32_t last_cidx;      /* 0x8C ELF */
    volatile uint32_t doorbells;      /* 0x90 ELF */
    volatile uint32_t polls;          /* 0x94 ELF */
    volatile uint32_t cycles;         /* 0x98 ELF: PMU cycles of the op (wraps) */
    volatile uint32_t heartbeat;      /* 0x9C ELF: counts while idle */
    volatile uint32_t probe_n;        /* 0xA0 host */
    volatile uint32_t probe_addr[8];  /* 0xA4 host */
    volatile uint32_t probe_val[8];   /* 0xC4 ELF */
    volatile uint32_t bdf_rb[6];      /* 0xE4 ELF: OP_BDF read-back */
};

static struct mbox *const mb = (struct mbox *)MBOX_ADDR;

static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }

static inline uint32_t rd32(uint32_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static inline uint32_t cycles(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(v));
    return v;
}

static void pmu_init(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(v));
    v |= 1u | (1u << 2);                                     /* E, reset the cycle counter */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(v));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(1u << 31));  /* enable PMCCNTR */
}

/* MPU: background region for everything else (SCTLR.BR), plus Device,
 * execute-never regions for the CPM paths: AxCACHE[1] = 0 (PG347). */
static void mpu_region(uint32_t n, uint32_t base, uint32_t log2size, uint32_t attr)
{
    __asm__ volatile("mcr p15, 0, %0, c6, c2, 0" :: "r"(n));            /* RGNR */
    __asm__ volatile("mcr p15, 0, %0, c6, c1, 0" :: "r"(base));         /* DRBAR */
    __asm__ volatile("mcr p15, 0, %0, c6, c1, 4" :: "r"(attr));         /* DRACR */
    __asm__ volatile("mcr p15, 0, %0, c6, c1, 2" :: "r"(((log2size - 1) << 1) | 1u)); /* DRSR */
}

#define DRACR_DEVICE_RW_XN ((1u << 12) | (3u << 8) | (1u << 2) | 1u)  /* XN, AP rw, shareable device */
#define DRACR_NORMAL_NC_RW ((3u << 8) | (1u << 3))                      /* TEX 001: normal non-cacheable */

static void mpu_init(void)
{
    uint32_t sctlr, nreg;

    dsb();
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    /* MPU, caches and high vectors off: the ELF may be loaded without a core reset,
     * on top of RP1's MPU/cache setup */
    sctlr &= ~((1u << 13) | (1u << 12) | (1u << 2) | 1u);   /* V, I, C, M */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
    isb();
    __asm__ volatile("mcr p15, 0, %0, c15, c5, 0" :: "r"(0));   /* invalidate all D-cache */
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));    /* invalidate all I-cache */
    dsb();
    isb();
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 4" : "=r"(nreg));    /* MPUIR */
    nreg = (nreg >> 8) & 0xFFu;                               /* DREGION: 12 or 16 */
    for (uint32_t n = 3; n < nreg; n++) {                     /* drop RP1's regions */
        __asm__ volatile("mcr p15, 0, %0, c6, c2, 0" :: "r"(n));
        __asm__ volatile("mcr p15, 0, %0, c6, c1, 2" :: "r"(0));
    }
    mpu_region(0, 0x30000000u, 26, DRACR_NORMAL_NC_RW | (1u << 12));   /* shared DDR 64 MiB */
    mpu_region(1, 0x90000000u, 28, DRACR_DEVICE_RW_XN);                /* CPM AXI slave 256 MiB */
    mpu_region(2, 0xFC000000u, 24, DRACR_DEVICE_RW_XN);                /* CPM registers 16 MiB */
    sctlr |= (1u << 17) | 1u;                                /* BR, M */
    dsb();
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
    isb();
}

/* data-abort recovery for OP_PROBE (see _dabort in boot.S) */
volatile uint32_t probe_armed, probe_fault, probe_dfsr, probe_dfar;

static int r5_addr_ok(uint32_t a)
{
    if (a & 3u)
        return 0;
    if (a >= R5_HOST_BASE && a < R5_HOST_BASE + 64u * R5_HOST_WIN)
        return 1;
    if (a >= R5_QDMA_BASE && a < R5_QDMA_BASE + R5_QDMA_SIZE)
        return 1;
    if (a >= 0xFCE90000u && a < 0xFCEB0000u)                 /* CPM5_DMA1_ATTR / _CSR only */
        return 1;
    return 0;
}

static void bdf_write(uint32_t n, uint32_t pa_lo, uint32_t pa_hi, uint32_t window)
{
    uint32_t e = BDF_TABLE + BDF_STRIDE * n;
    /* the six registers in order, smid (0x14) last, as host_probe.py does */
    wr32(e + 0x00, pa_lo & 0xFFFFF000u);
    wr32(e + 0x04, pa_hi);
    wr32(e + 0x08, 0);
    wr32(e + 0x0C, 0);
    wr32(e + 0x10, window);
    wr32(e + 0x14, 0);
    dsb();
    (void)rd32(e + 0x14);
}

static uint32_t bdf_window(uint32_t perm, uint32_t prot)
{
    /* access_perm [31:30], prot [28:26], win_size [25:0] in 4 KiB units */
    return (perm << 30) | ((prot & 7u) << 26) | (R5_HOST_WIN >> 12);
}

static uint32_t op_probe(void)
{
    uint32_t n = mb->probe_n, i;

    if (n > 8)
        return E_ADDR;
    for (i = 0; i < n; i++)
        if (!r5_addr_ok(mb->probe_addr[i]))
            return E_ADDR;
    mb->probe_fmask = 0;
    for (i = 0; i < n; i++) {
        uint32_t v = 0xDEADBEEFu;

        probe_fault = 0;
        probe_armed = 1;
        dsb();
        __asm__ volatile("ldr %0, [%1]" : "+r"(v) : "r"(mb->probe_addr[i]) : "memory");
        dsb();
        probe_armed = 0;
        mb->probe_val[i] = v;
        mb->probe_dfsr[i] = probe_fault ? probe_dfsr : 0;
        if (probe_fault)
            mb->probe_fmask |= 1u << i;
    }
    return E_OK;
}

static uint32_t op_bdf(void)
{
    uint32_t n = mb->bdf_entry, k, e;

    if (n > 63)
        return E_BDF;
    if (mb->pa_lo == 0 && mb->pa_hi == 0)
        bdf_write(n, 0, 0, 0);
    else
        bdf_write(n, mb->pa_lo & ~(R5_HOST_WIN - 1), mb->pa_hi, bdf_window(mb->bdf_perm, mb->bdf_prot));
    e = BDF_TABLE + BDF_STRIDE * n;
    for (k = 0; k < 6; k++)
        mb->bdf_rb[k] = rd32(e + 4 * k);
    return E_OK;
}

static uint32_t op_h2c(void)
{
    const uint32_t n = mb->rngsz, count = mb->count, db = mb->db_addr;
    const uint32_t batch = mb->batch ? mb->batch : 64u;
    const uint32_t flow = mb->flags & FLAG_FLOW;
    const uint32_t stall_limit = mb->stall_limit ? mb->stall_limit : 10000000u;
    uint32_t pidx = mb->start_pidx, cidx = mb->start_pidx, posted = 0, stall = 0;
    uint32_t wb = 0, ent = mb->bdf_entry, err = E_OK;
    uint32_t t0;

    if (db < R5_QDMA_BASE || db >= R5_QDMA_BASE + R5_QDMA_SIZE || (db & 3u))
        return E_ADDR;
    if (n < 2 || n > 65536 || pidx >= n)
        return E_RING;
    if (!flow && count > n - 1)
        return E_RING;          /* no CIDX: never more than rngsz - 1 outstanding */
    if (flow) {
        if (ent > 63 || (mb->pa_lo & 3u))
            return E_BDF;
        bdf_write(ent, mb->pa_lo & ~(R5_HOST_WIN - 1), mb->pa_hi, bdf_window(mb->bdf_perm, mb->bdf_prot));
        wb = R5_HOST_BASE + ent * R5_HOST_WIN + (mb->pa_lo & (R5_HOST_WIN - 1));
    }

    t0 = cycles();
    while (posted < count) {
        uint32_t freen, k;

        if (flow) {
            cidx = rd32(wb) >> 16;            /* { le16 pidx; le16 cidx } */
            mb->polls++;
            if (cidx >= n) {
                err = E_RING;
                break;
            }
        } else {
            cidx = mb->start_pidx;            /* assume nothing consumed: rngsz - 1 posts at most */
        }
        freen = (cidx + n - pidx - 1) % n;
        k = count - posted;
        if (k > batch)
            k = batch;
        if (k > freen)
            k = freen;
        if (!k) {
            if (++stall > stall_limit) {
                err = E_STALL;
                break;
            }
            continue;
        }
        stall = 0;
        pidx = (pidx + k) % n;
        dsb();
        wr32(db, pidx);                       /* pidx[15:0], irq_en 0 */
        posted += k;
        mb->doorbells++;
        mb->posted = posted;
        mb->final_pidx = pidx;
    }
    dsb();
    mb->cycles = cycles() - t0;
    mb->last_cidx = cidx;
    mb->posted = posted;
    mb->final_pidx = pidx;
    if (flow)
        bdf_write(ent, 0, 0, 0);              /* no window left behind */
    return err;
}

/* Data abort (e.g. a decode error on an address the static does not map):
 * report it and park; reload the ELF to continue. */
void rpuq_dabort(void)
{
    uint32_t dfar, dfsr;

    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    mb->last_cidx = dfar;
    mb->polls = dfsr;
    mb->err = E_ABORT;
    mb->state = ST_ERROR;
    dsb();
    mb->ack_seq = mb->cmd_seq;
    dsb();
    for (;;)
        __asm__ volatile("wfi");
}

void rpuq_main(void)
{
    uint32_t seen;

    mpu_init();
    pmu_init();
    mb->state = ST_IDLE;
    mb->version = MBOX_VERSION;
    seen = mb->cmd_seq;
    mb->ack_seq = seen;
    dsb();
    mb->magic = MBOX_MAGIC;

    for (;;) {
        uint32_t seq = mb->cmd_seq, err;

        if (seq == seen) {
            mb->heartbeat++;
            continue;
        }
        seen = seq;
        mb->state = ST_RUNNING;
        mb->err = 0;
        mb->posted = 0;
        mb->doorbells = 0;
        mb->polls = 0;
        dsb();
        switch (mb->op) {
        case OP_PROBE: err = op_probe(); break;
        case OP_H2C:   err = op_h2c(); break;
        case OP_BDF:   err = op_bdf(); break;
        default:       err = E_OP; break;
        }
        mb->err = err;
        mb->state = err ? ST_ERROR : ST_DONE;
        dsb();
        mb->ack_seq = seq;
        dsb();
    }
}
