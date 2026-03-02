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

#include "clock.h"

#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-journal.h>

// Offsets from xclk_wiz_hw.h
#define XCLK_WIZ_RECONFIG_OFFSET 0x00000014u
#define XCLK_WIZ_REG1_OFFSET     0x00000330u
#define XCLK_WIZ_REG2_OFFSET     0x00000334u
#define XCLK_WIZ_REG3_OFFSET     0x00000338u
#define XCLK_WIZ_REG4_OFFSET     0x0000033Cu
#define XCLK_WIZ_REG11_OFFSET    0x00000378u
#define XCLK_WIZ_REG12_OFFSET    0x00000380u
#define XCLK_WIZ_REG13_OFFSET    0x00000384u
#define XCLK_WIZ_REG14_OFFSET    0x00000398u
#define XCLK_WIZ_REG15_OFFSET    0x0000039Cu
#define XCLK_WIZ_REG16_OFFSET    0x000003A0u
#define XCLK_WIZ_REG17_OFFSET    0x000003A8u
#define XCLK_WIZ_REG19_OFFSET    0x000003CCu
#define XCLK_WIZ_REG25_OFFSET    0x000003F0u
#define XCLK_WIZ_REG26_OFFSET    0x000003FCu

// Bit masks/shifts
#define XCLK_WIZ_LOCK               0x1u
#define XCLK_WIZ_RECONFIG_LOAD      0x1u
#define XCLK_WIZ_RECONFIG_SADDR     0x2u

#define XCLK_WIZ_REG1_EDGE_MASK     0x100u
#define XCLK_WIZ_CLKFBOUT_L_MASK    0xFFu
#define XCLK_WIZ_CLKFBOUT_H_MASK    0xFF00u
#define XCLK_WIZ_CLKFBOUT_H_SHIFT   8u

#define XCLK_WIZ_EDGE_MASK          (1u << 10)
#define XCLK_WIZ_P5EN_MASK          (1u << 8)

#define XCLK_WIZ_REG3_PREDIV2       (1u << 11)
#define XCLK_WIZ_REG3_USED          (1u << 12)
#define XCLK_WIZ_REG3_MX            (1u << 9)

#define XCLK_WIZ_REG1_PREDIV2       (1u << 12)
#define XCLK_WIZ_REG1_EN            (1u << 9)
#define XCLK_WIZ_REG1_MX            (1u << 10)

#define XCLK_WIZ_CLKOUT0_P5EN_SHIFT    13u
#define XCLK_WIZ_CLKOUT0_P5FEDGE_SHIFT 15u
#define XCLK_WIZ_REG12_EDGE_SHIFT      10u

#define XCLK_MHZ 1000000ull

// Versal limits (from header)
#define XCLK_M_MIN 4u
#define XCLK_M_MAX 432u
#define XCLK_D_MIN 1u
#define XCLK_D_MAX 123u
#define XCLK_VCO_MIN 2160u
#define XCLK_VCO_MAX 4320u
#define XCLK_O_MIN 2u
#define XCLK_O_MAX 511u

#define CLOCK_DRIVER_DEFAULT_PRIM_IN_HZ 100000000u
#define CLOCK_DRIVER_DEFAULT_MIN_ERR_HZ 500000u
#define CLOCK_DRIVER_DEFAULT_MAX_CANDIDATES 50u
#define CLOCK_DRIVER_DEFAULT_O_WINDOW 6u
#define CLOCK_DRIVER_DEFAULT_LOCK_TIMEOUT_MS 200u

static int clock_driver_init(struct clock_driver *clk, struct slash_ctldev *ctl)
{
    if (clk == NULL || ctl == NULL) {
        errno = EINVAL;
        return -1;
    }

    *clk = (struct clock_driver) {
        .ctl = ctl,
        .bar = NULL,
        .regs = NULL,
        .len = 0,
        .prim_in_hz = CLOCK_DRIVER_DEFAULT_PRIM_IN_HZ,
        .m = 0,
        .d = 0,
        .o = 0,
        .min_err_hz = CLOCK_DRIVER_DEFAULT_MIN_ERR_HZ,
    };

    clk->bar = slash_bar_file_open(ctl, CLOCK_DRIVER_BAR_NUMBER, O_CLOEXEC);
    if (clk->bar == NULL) {
        return -1;
    }

    clk->regs = (volatile uint32_t *) clk->bar->map;
    clk->len = clk->bar->len;
    if (clk->regs == NULL || clk->len == 0) {
        return -1;
    }

    return 0;
}

struct clock_driver *clock_driver_create(struct slash_ctldev *ctl)
{
    struct clock_driver *clk = calloc(1, sizeof(*clk));
    if (clk == NULL) {
        (void) sd_journal_print(LOG_ERR, "Failed to allocate clock driver: %m");
        return NULL;
    }

    if (clock_driver_init(clk, ctl) != 0) {
        (void) sd_journal_print(LOG_ERR, "Failed to initialize clock driver: %m");
        cleanup_clock_driver(clk);
        return NULL;
    }

    return clk;
}

void cleanup_clock_driver(struct clock_driver *clk)
{
    if (clk == NULL) {
        return;
    }

    if (clk->bar != NULL) {
        (void) slash_bar_file_close(clk->bar);
        clk->bar = NULL;
    }

    clk->regs = NULL;
    clk->len = 0;
    clk->ctl = NULL;

    free(clk);
}

static int clock_driver_check_bounds(const struct clock_driver *clk, uint32_t offset)
{
    if (clk == NULL || clk->regs == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (offset + sizeof(uint32_t) > clk->len) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static inline uint32_t clock_driver_r32(struct clock_driver *clk, uint32_t offset)
{
    return clk->regs[offset / sizeof(uint32_t)];
}

static inline void clock_driver_w32(struct clock_driver *clk, uint32_t offset, uint32_t value)
{
    clk->regs[offset / sizeof(uint32_t)] = value;
}

static inline uint32_t clock_driver_reg(uint32_t wizard_offset, uint32_t reg_offset)
{
    return wizard_offset + reg_offset;
}

static int clock_driver_check_wizard_bounds(const struct clock_driver *clk, uint32_t wizard_offset)
{
    return clock_driver_check_bounds(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG26_OFFSET));
}

static uint64_t clock_driver_get_vco_hz(struct clock_driver *clk, uint32_t wizard_offset)
{
    uint32_t reg = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG1_OFFSET));
    uint32_t edge = (reg & XCLK_WIZ_REG1_EDGE_MASK) ? 1u : 0u;

    reg = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG2_OFFSET));
    uint32_t low = reg & XCLK_WIZ_CLKFBOUT_L_MASK;
    uint32_t high = (reg & XCLK_WIZ_CLKFBOUT_H_MASK) >> XCLK_WIZ_CLKFBOUT_H_SHIFT;
    uint32_t mult = low + high + edge;
    if (mult == 0) {
        mult = 1;
    }

    reg = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG13_OFFSET));
    low = reg & XCLK_WIZ_CLKFBOUT_L_MASK;
    high = (reg & XCLK_WIZ_CLKFBOUT_H_MASK) >> XCLK_WIZ_CLKFBOUT_H_SHIFT;

    reg = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG12_OFFSET));
    edge = (reg & XCLK_WIZ_EDGE_MASK) ? 1u : 0u;

    uint32_t div = low + high + edge;
    if (div == 0) {
        div = 1;
    }

    return ((uint64_t)clk->prim_in_hz * mult) / div;
}

static uint64_t clock_driver_get_rate_hz(struct clock_driver *clk, uint32_t wizard_offset, uint32_t clock_id)
{
    uint64_t fvco = clock_driver_get_vco_hz(clk, wizard_offset);
    uint32_t leaf_off = (clock_id < 3)
        ? (XCLK_WIZ_REG3_OFFSET + clock_id * 8u)
        : (XCLK_WIZ_REG19_OFFSET + clock_id * 8u);
    uint32_t reg_off = clock_driver_reg(wizard_offset, leaf_off);

    uint32_t reg = clock_driver_r32(clk, reg_off);
    uint32_t edge = (reg & (1u << XCLK_WIZ_CLKOUT0_P5FEDGE_SHIFT)) ? 1u : 0u;
    uint32_t p5en = (reg & XCLK_WIZ_P5EN_MASK) ? 1u : 0u;
    uint32_t prediv = (reg & XCLK_WIZ_REG3_PREDIV2) ? 1u : 0u;

    uint32_t reg2 = clock_driver_r32(clk, reg_off + 4u);
    uint32_t low = reg2 & XCLK_WIZ_CLKFBOUT_L_MASK;
    uint32_t high = (reg2 & XCLK_WIZ_CLKFBOUT_H_MASK) >> XCLK_WIZ_CLKFBOUT_H_SHIFT;
    uint32_t leaf = high + low + edge;
    uint32_t divo = (prediv + 1u) * leaf + (prediv * p5en);
    if (divo == 0) {
        divo = 1;
    }

    return fvco / divo;
}

static uint32_t clock_driver_effective_divo_from_o(uint32_t o)
{
    if (o > XCLK_O_MAX) {
        o = XCLK_O_MAX;
    }

    uint32_t high_time = o / 4u;
    uint32_t edge = o % 2u;
    uint32_t p5en = ((o % 4u) <= 1u) ? 0u : 1u;
    uint32_t leaf = (high_time * 2u) + edge;
    uint32_t divo = (2u * leaf) + p5en;
    if (divo == 0) {
        divo = 1u;
    }

    return divo;
}

static void clock_driver_log_state(
    struct clock_driver *clk,
    uint32_t wizard_offset,
    uint32_t clock_id,
    const char *stage
)
{
    uint32_t leaf_off = (clock_id < 3)
        ? (XCLK_WIZ_REG3_OFFSET + clock_id * 8u)
        : (XCLK_WIZ_REG19_OFFSET + clock_id * 8u);
    uint32_t leaf_reg_off = clock_driver_reg(wizard_offset, leaf_off);

    uint32_t reg1 = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG1_OFFSET));
    uint32_t reg2 = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG2_OFFSET));
    uint32_t reg12 = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG12_OFFSET));
    uint32_t reg13 = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG13_OFFSET));
    uint32_t leaf0 = clock_driver_r32(clk, leaf_reg_off);
    uint32_t leaf1 = clock_driver_r32(clk, leaf_reg_off + 4u);
    uint32_t status = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG4_OFFSET));
    uint32_t reconfig = clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_RECONFIG_OFFSET));

    uint64_t fvco_hz = clock_driver_get_vco_hz(clk, wizard_offset);
    uint64_t rate_hz = clock_driver_get_rate_hz(clk, wizard_offset, clock_id);

    (void) sd_journal_print(
        LOG_INFO,
        "clock_driver[%s]: wiz=0x%08x clk=%u prim_in_hz=%u fvco_hz=%" PRIu64
        " rate_hz=%" PRIu64 " status=0x%08x reconfig=0x%08x reg1=0x%08x reg2=0x%08x"
        " reg12=0x%08x reg13=0x%08x leaf0=0x%08x leaf1=0x%08x",
        stage,
        wizard_offset,
        clock_id,
        clk->prim_in_hz,
        fvco_hz,
        rate_hz,
        status,
        reconfig,
        reg1,
        reg2,
        reg12,
        reg13,
        leaf0,
        leaf1
    );
}

static void clock_driver_update_o(struct clock_driver *clk, uint32_t wizard_offset, uint32_t clock_id)
{
    uint32_t o = clk->o;
    if (o > XCLK_O_MAX) {
        o = XCLK_O_MAX;
    }

    uint32_t leaf_off = (clock_id < 3)
        ? (XCLK_WIZ_REG3_OFFSET + clock_id * 8u)
        : (XCLK_WIZ_REG19_OFFSET + clock_id * 8u);
    uint32_t reg_off = clock_driver_reg(wizard_offset, leaf_off);

    uint32_t high_time = o / 4u;
    uint32_t reg = XCLK_WIZ_REG3_PREDIV2 | XCLK_WIZ_REG3_USED | XCLK_WIZ_REG3_MX;

    uint32_t div_edge = ((o % 4u) <= 1u) ? 0u : 1u;
    reg |= (div_edge << 8u);

    uint32_t p5f_edge = o % 2u;
    uint32_t p5_enable = o % 2u;
    reg |= (p5_enable << XCLK_WIZ_CLKOUT0_P5EN_SHIFT) |
           (p5f_edge << XCLK_WIZ_CLKOUT0_P5FEDGE_SHIFT);

    clock_driver_w32(clk, reg_off, reg);
    clock_driver_w32(clk, reg_off + 4u, (high_time | (high_time << 8u)));
}

static void clock_driver_update_d(struct clock_driver *clk, uint32_t wizard_offset)
{
    uint32_t d = clk->d;
    uint32_t high_time = d / 2u;

    uint32_t reg = 0;
    reg &= ~(1u << XCLK_WIZ_REG12_EDGE_SHIFT);
    uint32_t div_edge = d % 2u;
    reg |= (div_edge << XCLK_WIZ_REG12_EDGE_SHIFT);

    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG12_OFFSET), reg);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG13_OFFSET), (high_time | (high_time << 8u)));
}

static void clock_driver_update_m(struct clock_driver *clk, uint32_t wizard_offset)
{
    uint32_t m = clk->m;
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG25_OFFSET), 0);

    uint32_t div_edge = m % 2u;
    uint32_t high_time = m / 2u;
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG2_OFFSET), (high_time | (high_time << 8u)));

    uint32_t reg = XCLK_WIZ_REG1_PREDIV2 | XCLK_WIZ_REG1_EN | XCLK_WIZ_REG1_MX;
    if (div_edge) {
        reg |= (1u << 8u);
    } else {
        reg &= ~(1u << 8u);
    }
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG1_OFFSET), reg);
}

static void clock_driver_program_common_tail(struct clock_driver *clk, uint32_t wizard_offset)
{
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG11_OFFSET), 0x2Eu);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG14_OFFSET), 0xE80u);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG15_OFFSET), 0x4271u);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG16_OFFSET), 0x43E9u);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG17_OFFSET), 0x001Cu);
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG26_OFFSET), 0x0001u);
}

static void clock_driver_trigger_reconfig(struct clock_driver *clk, uint32_t wizard_offset)
{
    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_RECONFIG_OFFSET),
                     (XCLK_WIZ_RECONFIG_LOAD | XCLK_WIZ_RECONFIG_SADDR));
}

static int clock_driver_wait_for_lock(struct clock_driver *clk, uint32_t wizard_offset, uint32_t timeout_ms)
{
    struct timespec start;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return -1;
    }

    for (;;) {
        if ((clock_driver_r32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG4_OFFSET)) & XCLK_WIZ_LOCK) != 0u) {
            return 0;
        }

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            return -1;
        }

        time_t sec = now.tv_sec - start.tv_sec;
        long nsec_signed = now.tv_nsec - start.tv_nsec;
        if (nsec_signed < 0) {
            sec -= 1;
            nsec_signed += 1000000000l;
        }

        uint64_t elapsed_ms = (uint64_t)sec * 1000ull;
        uint64_t nsec = (uint64_t)nsec_signed;
        elapsed_ms += nsec / 1000000ull;

        if (elapsed_ms > timeout_ms) {
            errno = ETIMEDOUT;
            return -1;
        }

        (void) usleep(100);
    }
}

static uint64_t clock_driver_program_mdo_and_reconfig(
    struct clock_driver *clk,
    uint32_t wizard_offset,
    uint32_t clock_id,
    uint32_t timeout_ms,
    int *ok
)
{
    *ok = -1;

    clock_driver_w32(clk, clock_driver_reg(wizard_offset, XCLK_WIZ_REG25_OFFSET), 0);

    clock_driver_update_o(clk, wizard_offset, clock_id);
    clock_driver_update_d(clk, wizard_offset);
    clock_driver_update_m(clk, wizard_offset);
    clock_driver_program_common_tail(clk, wizard_offset);

    clock_driver_trigger_reconfig(clk, wizard_offset);

    if (clock_driver_wait_for_lock(clk, wizard_offset, timeout_ms) != 0) {
        return 0;
    }

    *ok = 0;
    return clock_driver_get_rate_hz(clk, wizard_offset, clock_id);
}

struct clock_candidate {
    uint64_t diff_hz;
    uint64_t achieved_hz;
    uint32_t m;
    uint32_t d;
    uint32_t o;
    uint64_t fvco_hz;
};

static int clock_driver_candidate_cmp(
    const struct clock_candidate *a,
    const struct clock_candidate *b
)
{
    if (a->diff_hz != b->diff_hz) {
        return (a->diff_hz < b->diff_hz) ? -1 : 1;
    }

    // Prefer o divisible by 4 (stable/easy-to-represent output divisors).
    uint32_t ao4 = (a->o % 4u == 0u) ? 0u : 1u;
    uint32_t bo4 = (b->o % 4u == 0u) ? 0u : 1u;
    if (ao4 != bo4) {
        return (ao4 < bo4) ? -1 : 1;
    }

    // Then prefer even o, then higher VCO for better jitter behavior.
    uint32_t ao2 = (a->o % 2u == 0u) ? 0u : 1u;
    uint32_t bo2 = (b->o % 2u == 0u) ? 0u : 1u;
    if (ao2 != bo2) {
        return (ao2 < bo2) ? -1 : 1;
    }

    if (a->fvco_hz != b->fvco_hz) {
        return (a->fvco_hz > b->fvco_hz) ? -1 : 1;
    }

    if (a->o != b->o) {
        return (a->o < b->o) ? -1 : 1;
    }

    return 0;
}

static size_t clock_driver_generate_candidates(
    struct clock_driver *clk,
    uint32_t target_hz,
    struct clock_candidate *cands,
    size_t max_candidates
)
{
    if (clk == NULL || target_hz == 0 || cands == NULL || max_candidates == 0) {
        return 0;
    }

    uint64_t vco_min_hz = (uint64_t)XCLK_VCO_MIN * XCLK_MHZ;
    uint64_t vco_max_hz = (uint64_t)XCLK_VCO_MAX * XCLK_MHZ;
    size_t count = 0;

    for (uint32_t m = XCLK_M_MIN; m <= XCLK_M_MAX; ++m) {
        uint64_t numerator = (uint64_t)clk->prim_in_hz * m;
        for (uint32_t d = XCLK_D_MIN; d <= XCLK_D_MAX; ++d) {
            uint64_t fvco_hz = numerator / d;
            if (fvco_hz < vco_min_hz || fvco_hz > vco_max_hz) {
                continue;
            }

            uint64_t o_est = (fvco_hz + target_hz / 2u) / target_hz;
            if (o_est < XCLK_O_MIN) {
                o_est = XCLK_O_MIN;
            } else if (o_est > XCLK_O_MAX) {
                o_est = XCLK_O_MAX;
            }

            uint32_t o_lo = (o_est > CLOCK_DRIVER_DEFAULT_O_WINDOW)
                ? (uint32_t)(o_est - CLOCK_DRIVER_DEFAULT_O_WINDOW)
                : XCLK_O_MIN;
            if (o_lo < XCLK_O_MIN) {
                o_lo = XCLK_O_MIN;
            }

            uint32_t o_hi = (uint32_t)(o_est + CLOCK_DRIVER_DEFAULT_O_WINDOW);
            if (o_hi > XCLK_O_MAX) {
                o_hi = XCLK_O_MAX;
            }

            for (uint32_t o = o_lo; o <= o_hi; ++o) {
                uint64_t achieved_hz = fvco_hz / o;
                uint64_t diff_hz = (achieved_hz > target_hz)
                    ? (achieved_hz - target_hz)
                    : (target_hz - achieved_hz);
                struct clock_candidate candidate = {
                    .diff_hz = diff_hz,
                    .achieved_hz = achieved_hz,
                    .m = m,
                    .d = d,
                    .o = o,
                    .fvco_hz = fvco_hz,
                };

                if (count < max_candidates) {
                    cands[count++] = candidate;
                    continue;
                }

                size_t worst_index = 0;
                for (size_t i = 1; i < count; ++i) {
                    if (clock_driver_candidate_cmp(&cands[worst_index], &cands[i]) < 0) {
                        worst_index = i;
                    }
                }
                if (clock_driver_candidate_cmp(&candidate, &cands[worst_index]) < 0) {
                    cands[worst_index] = candidate;
                }
            }
        }
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (clock_driver_candidate_cmp(&cands[j], &cands[i]) < 0) {
                struct clock_candidate tmp = cands[i];
                cands[i] = cands[j];
                cands[j] = tmp;
            }
        }
    }

    return count;
}

static int clock_driver_try_set_rate_hz(
    struct clock_driver *clk,
    uint32_t wizard_offset,
    uint32_t clock_id,
    uint32_t *rate_hz_inout
)
{
    if (clk == NULL || rate_hz_inout == NULL || *rate_hz_inout == 0) {
        errno = EINVAL;
        (void) sd_journal_print(
            LOG_WARNING,
            "clock_driver: invalid set_rate arguments (clk=%p rate_ptr=%p rate_hz=%u)",
            (void *)clk,
            (void *)rate_hz_inout,
            (rate_hz_inout != NULL) ? *rate_hz_inout : 0u
        );
        return -1;
    }

    struct clock_candidate candidates[CLOCK_DRIVER_DEFAULT_MAX_CANDIDATES];
    size_t count = clock_driver_generate_candidates(
        clk,
        *rate_hz_inout,
        candidates,
        CLOCK_DRIVER_DEFAULT_MAX_CANDIDATES
    );
    if (count == 0) {
        errno = ERANGE;
        (void) sd_journal_print(
            LOG_WARNING,
            "clock_driver: failed to calculate divisors for request_hz=%u: %m",
            *rate_hz_inout
        );
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        const struct clock_candidate *cand = &candidates[i];
        clk->m = cand->m;
        clk->d = cand->d;
        clk->o = cand->o;

        uint64_t predicted_fvco_hz = ((uint64_t)clk->prim_in_hz * clk->m) / clk->d;
        uint32_t predicted_divo = clock_driver_effective_divo_from_o(clk->o);
        uint64_t predicted_rate_hz = predicted_fvco_hz / predicted_divo;
        (void) sd_journal_print(
            LOG_INFO,
            "clock_driver: request_hz=%u trying candidate=%zu/%zu m=%u d=%u o=%u est_hz=%" PRIu64
            " diff_hz=%" PRIu64 " predicted_divo=%u predicted_rate_hz=%" PRIu64,
            *rate_hz_inout,
            i + 1u,
            count,
            clk->m,
            clk->d,
            clk->o,
            cand->achieved_hz,
            cand->diff_hz,
            predicted_divo,
            predicted_rate_hz
        );

        clock_driver_log_state(clk, wizard_offset, clock_id, "before_program");

        int ok = 0;
        uint64_t reported = clock_driver_program_mdo_and_reconfig(
            clk, wizard_offset, clock_id, CLOCK_DRIVER_DEFAULT_LOCK_TIMEOUT_MS, &ok
        );

        clock_driver_log_state(clk, wizard_offset, clock_id, "after_program");

        if (ok == 0) {
            (void) sd_journal_print(
                LOG_INFO,
                "clock_driver: set_rate request_hz=%u reported_hz=%" PRIu64
                " m=%u d=%u o=%u candidate=%zu/%zu",
                *rate_hz_inout,
                reported,
                clk->m,
                clk->d,
                clk->o,
                i + 1u,
                count
            );
            *rate_hz_inout = (uint32_t)reported;
            return 0;
        }

        (void) sd_journal_print(
            LOG_WARNING,
            "clock_driver: lock timeout request_hz=%u candidate=%zu/%zu m=%u d=%u o=%u timeout_ms=%u",
            *rate_hz_inout,
            i + 1u,
            count,
            clk->m,
            clk->d,
            clk->o,
            CLOCK_DRIVER_DEFAULT_LOCK_TIMEOUT_MS
        );
    }

    errno = ETIMEDOUT;
    return -1;
}

int clock_driver_get_service_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_out)
{
    if (rate_hz_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (clock_driver_check_wizard_bounds(clk, CLOCK_DRIVER_SERVICE_REGION_WIZARD_OFFSET) != 0) {
        return -1;
    }

    uint64_t rate = clock_driver_get_rate_hz(
        clk,
        CLOCK_DRIVER_SERVICE_REGION_WIZARD_OFFSET,
        CLOCK_DRIVER_WIZARD_CLKOUT_ID
    );
    *rate_hz_out = (uint32_t)rate;
    return 0;
}

int clock_driver_set_service_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_inout)
{
    if (clock_driver_check_wizard_bounds(clk, CLOCK_DRIVER_SERVICE_REGION_WIZARD_OFFSET) != 0) {
        (void) sd_journal_print(LOG_ERR, "clock_driver: service region wizard bounds check failed: %m");
        return -1;
    }
    int ret = clock_driver_try_set_rate_hz(
        clk,
        CLOCK_DRIVER_SERVICE_REGION_WIZARD_OFFSET,
        CLOCK_DRIVER_WIZARD_CLKOUT_ID,
        rate_hz_inout
    );
    if (ret != 0) {
        (void) sd_journal_print(
            LOG_WARNING,
            "clock_driver: failed to set service region frequency request_hz=%u: %m",
            (rate_hz_inout != NULL) ? *rate_hz_inout : 0u
        );
    }
    return ret;
}

int clock_driver_get_user_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_out)
{
    if (rate_hz_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (clock_driver_check_wizard_bounds(clk, CLOCK_DRIVER_USER_REGION_WIZARD_OFFSET) != 0) {
        return -1;
    }

    uint64_t rate = clock_driver_get_rate_hz(
        clk,
        CLOCK_DRIVER_USER_REGION_WIZARD_OFFSET,
        CLOCK_DRIVER_WIZARD_CLKOUT_ID
    );
    *rate_hz_out = (uint32_t)rate;
    return 0;
}

int clock_driver_set_user_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_inout)
{
    if (clock_driver_check_wizard_bounds(clk, CLOCK_DRIVER_USER_REGION_WIZARD_OFFSET) != 0) {
        (void) sd_journal_print(LOG_ERR, "clock_driver: user region wizard bounds check failed: %m");
        return -1;
    }
    int ret = clock_driver_try_set_rate_hz(
        clk,
        CLOCK_DRIVER_USER_REGION_WIZARD_OFFSET,
        CLOCK_DRIVER_WIZARD_CLKOUT_ID,
        rate_hz_inout
    );
    if (ret != 0) {
        (void) sd_journal_print(
            LOG_WARNING,
            "clock_driver: failed to set user region frequency request_hz=%u: %m",
            (rate_hz_inout != NULL) ? *rate_hz_inout : 0u
        );
    }
    return ret;
}
