#!/usr/bin/env python3
# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

"""Frequency of the V80 QSFP reference clocks against the host clock.

Each eth_test counts its DCMAC's clock wizard output (CYCLES, 64 bit). That clock
comes from the QSFP GT reference clock (IBUFDS_GTME5 ODIV2 -> BUFG_GT -> MMCM):
nominal 322.265625 MHz x 194.125 / 16 / 17 = 229.99932 MHz (M/D/O from the qsfp7
timing report: VCO 3910 MHz, clk_out4 = VCO / 17). The offset in ppm of the
count against the host's CLOCK_MONOTONIC (NTP disciplined) is the offset of the
reference clock, and so of the GT line rate. 100GBASE-R allows +-100 ppm.

  sudo ./clkppm.py [-d 0000:1b:00] [--seconds 30]
"""
import argparse
import time

import qsfp_loop as q

REFCLK = 322.265625e6
NOMINAL = REFCLK * 194.125 / 16 / 17


def sample(p):
    t0 = time.monotonic()
    c = p.rd64(q.CYCLES)
    t1 = time.monotonic()
    return c, (t0 + t1) / 2, t1 - t0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="0000:1b:00")
    ap.add_argument("--seconds", type=float, default=30.0)
    args = ap.parse_args()
    bar = q.Bar(args.device)
    ports = {0: q.Port(bar, 0), 2: q.Port(bar, 2)}      # DCMAC0 (qsfp0 refclk), DCMAC1 (qsfp2 refclk)
    first = {n: sample(p) for n, p in ports.items()}
    time.sleep(args.seconds)
    last = {n: sample(p) for n, p in ports.items()}
    for n in ports:
        (c0, t0, e0), (c1, t1, e1) = first[n], last[n]
        f = (c1 - c0) / (t1 - t0)
        ppm = (f / NOMINAL - 1) * 1e6
        err = (e0 + e1) / (t1 - t0) * 1e6
        print(f"DCMAC{ports[n].dc} (QSFP{n} tester): {f / 1e6:.6f} MHz, nominal {NOMINAL / 1e6:.6f} MHz, "
              f"{ppm:+.1f} ppm (+-{err:.2f} ppm read timing) -> refclk {REFCLK * (1 + ppm * 1e-6) / 1e6:.6f} MHz")


if __name__ == "__main__":
    main()
