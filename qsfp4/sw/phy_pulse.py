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

"""Pulse one PHY_CTRL reset bit of one QSFP port, then show the PHY state of
every port. For narrowing down which GT reset a quad fails to come back from.

  phy_pulse.py PORT BIT [--wait S] [--trace S]
    BIT 4  GT RX datapath reset
        5  GT RX PLL and datapath reset
        6  GT TX datapath reset
        7  rerun the DCMAC/GT reset sequencer (GT reset of both quads of the DCMAC)

--trace S samples PHY_STATUS of the port as fast as Python allows for S seconds
from the start of the pulse and prints every change with its time, e.g. to see
whether the sequencer takes the AXIS out of reset (bit 7) before the GT quads
are done again (bits 0, 1).
"""
import argparse
import sys
import time

import qsfp_loop as q


def show(st):
    return " ".join(f"{b}={st >> i & 1}" for i, b in enumerate(q.PHY_BITS))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="0000:1b:00")
    ap.add_argument("port", type=int, choices=sorted(q.PORTS))
    ap.add_argument("bit", type=int, choices=(4, 5, 6, 7))
    ap.add_argument("--wait", type=float, default=2.0)
    ap.add_argument("--trace", type=float, default=0.0)
    args = ap.parse_args()
    bar = q.Bar(args.device)
    p = q.Port(bar, args.port)
    st = p.rd(q.PHY_STATUS)
    print(f"QSFP{args.port}: pulse PHY_CTRL bit {args.bit}, PHY_STATUS before {st:#06x}")
    if args.trace:
        ctrl = p.rd(q.PHY_CTRL) & ~0xF0
        rows, last, released = [], None, False
        t0 = time.perf_counter()
        p.wr(q.PHY_CTRL, ctrl | 1 << args.bit)
        while True:
            t = time.perf_counter() - t0
            if not released and t >= 0.01:
                p.wr(q.PHY_CTRL, ctrl)
                released = True
                rows.append((t, None))
            if t >= args.trace:
                break
            st = p.rd(q.PHY_STATUS)
            if st != last:
                rows.append((t, st))
                last = st
        if not released:
            p.wr(q.PHY_CTRL, ctrl)
        for t, st in rows:
            print(f"  {t * 1e3:9.3f} ms  " + ("bit released" if st is None else f"{st:#06x}  {show(st)}"))
    else:
        p.pulse(args.bit)
    t0 = time.time()
    st = p.rd(q.PHY_STATUS)
    while time.time() - t0 < args.wait and (st & 3) != 3:
        time.sleep(0.01)
        st = p.rd(q.PHY_STATUS)
    done = time.time() - t0
    print(f"QSFP{args.port}: tx/rx reset done {st & 1}/{st >> 1 & 1} after {done:.2f} s")
    time.sleep(max(0.0, args.wait - done))
    for n in q.PORTS:
        q.phy_show(q.Port(bar, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
