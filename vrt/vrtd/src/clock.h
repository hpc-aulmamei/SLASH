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

#ifndef VRTD_CLOCK_H
#define VRTD_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#include <slash/ctldev.h>

// BAR index used by the clock driver.
#define CLOCK_DRIVER_BAR_NUMBER 4

// Clock wizard register windows inside BAR4.
#define CLOCK_DRIVER_USER_REGION_WIZARD_OFFSET 0x00000000u
#define CLOCK_DRIVER_SERVICE_REGION_WIZARD_OFFSET 0x00010000u

// Each wizard exposes clk_out1 as output index 0.
#define CLOCK_DRIVER_WIZARD_CLKOUT_ID 0u

struct clock_driver {
    struct slash_ctldev *ctl; /* non-owning */
    struct slash_bar_file *bar; /* owning */
    volatile uint32_t *regs;
    size_t len;
    uint32_t prim_in_hz;
    uint32_t m;
    uint32_t d;
    uint32_t o;
    uint32_t min_err_hz;
};

struct clock_driver *clock_driver_create(struct slash_ctldev *ctl);
void cleanup_clock_driver(struct clock_driver *clk);
static inline
void cleanup_clock_driverp(struct clock_driver **clkp)
{
    cleanup_clock_driver(*clkp);
    *clkp = NULL;
}

int clock_driver_get_service_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_out);
int clock_driver_set_service_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_inout);

int clock_driver_get_user_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_out);
int clock_driver_set_user_region_rate_hz(struct clock_driver *clk, uint32_t *rate_hz_inout);

#endif // VRTD_CLOCK_H
