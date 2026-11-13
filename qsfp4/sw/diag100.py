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

"""100G data corruption: which reset clears it, and whether it comes back
after a rate switch. All four ports in near-end PMA loopback (no cable).

  diag100.py resets [--len L]     at the current rate: probe all ports; for each
                                  failing port try, probing after each, its RX
                                  reset, its DCMAC TX serdes reset, its GT TX
                                  reset (with the serdes hold), then the
                                  sequencer of its DCMAC
  diag100.py cycle N [--len L]    N times: 200G, probe, 100G, probe
  diag100.py prime L1 L2 ...      for each Li: at 200G one frame of Li bytes on
                                  every port (it decides on which segments the
                                  last 200G RX cycle ends), then 100G and probe
"""
import argparse
import sys
import time

import qsfp_loop as q


def detail(p, frames):
    s = q.snapshot(p)
    f = p.rd(q.FIRST)
    avg = s["rx_bytes"] / s["rx_frames"] if s["rx_frames"] else 0
    out = (f"rx {s['rx_frames']}/{frames} hdr {s['hdr_err']} len {s['len_err']} pay {s['pay_err']} "
           f"reorder {s['reorder']} from {s['last_id']} avg len field {avg:.0f}")
    if f >> 31:
        kinds = "+".join(k for i, k in enumerate(["payload", "header", "length"]) if f >> (16 + i) & 1)
        out += (f"; first {kinds} lane {f >> 20 & 15} beat {f & 0xFFFF} "
                f"got {p.rd64(q.FIRST_GOT):#018x} exp {p.rd64(q.FIRST_EXP):#018x}")
    return out


def probe(ports, args, label):
    peer = {n: n for n in ports}
    bad = q.probe(ports, peer, args)
    for n, p in ports.items():
        print(f"  {label:28} QSFP{n}: {'BAD  ' + detail(p, 2000) if n in bad else 'ok'}", flush=True)
    return bad


def wait_links(ports, timeout=5.0):
    t_end = time.time() + timeout
    while time.time() < t_end:
        if all(p.link()[0] for p in ports.values()):
            time.sleep(0.2)
            return True
        time.sleep(0.05)
    for n, p in ports.items():
        if not p.link()[0]:
            print(f"  QSFP{n}: link down, rx phy {p.link()[1]:#05x}", flush=True)
    return False


def links_200(bar, ports, args, label):
    """After a switch to 200G: links on their own within 5 s, else show the PHY
    state and recover with qsfp_loop's bring_up (RX resets, then the DCMAC
    sequencer). Returns how the links came up."""
    if wait_links(ports):
        return "by themselves"
    for p in ports.values():
        q.phy_show(p)
    print(f"  {label}: links down after 5 s, bring_up", flush=True)
    return "after bring_up" if q.bring_up(bar, ports, {n: n for n in ports}, args) else "NOT up"


def cmd_resets(bar, args):
    ports = q.all_ports(bar)
    q.set_loopbacks(ports, {n: n for n in ports})
    wait_links(ports)
    bad = probe(ports, args, "start")
    for n in sorted(bad):
        p = ports[n]
        one = {n: p}
        steps = [("RX reset", p.rx_reset),
                 ("DCMAC TX serdes reset", p.tx_serdes_reset),
                 ("GT TX reset + serdes hold", p.tx_reset),
                 (f"DCMAC{p.dc} sequencer", lambda: q.restart_dcmac(bar, p.dc))]
        for name, fn in steps:
            fn()
            wait_links(ports)
            if not probe(one, args, f"QSFP{n} after {name}"):
                print(f"QSFP{n}: fixed by {name}", flush=True)
                break
        else:
            print(f"QSFP{n}: not fixed", flush=True)
    probe(ports, args, "end")
    return 0


def cmd_cycle(bar, args):
    ports = q.all_ports(bar)
    tally = {n: {100: 0, 200: 0} for n in ports}
    for i in range(args.n):
        for rate in (200, 100):
            fec = "rs528" if rate == 100 else "rs544-200g"
            for dc in (0, 1):
                q.set_speed(bar, dc, rate, fec)
            q.set_loopbacks(ports, {n: n for n in ports})
            if rate == 200:
                print(f"  round {i + 1} 200G links {links_200(bar, ports, args, f'round {i + 1}')}", flush=True)
            else:
                wait_links(ports)
            bad = probe(ports, args, f"round {i + 1} {rate}G")
            for n in ports:
                tally[n][rate] += n not in bad
    for n in ports:
        print(f"QSFP{n}: clean in {tally[n][200]}/{args.n} at 200G, {tally[n][100]}/{args.n} at 100G")
    return 0


def cmd_prime(bar, args):
    ports = q.all_ports(bar)
    peer = {n: n for n in ports}
    tally = {}
    for plen in args.lens:
        for dc in (0, 1):
            q.set_speed(bar, dc, 200, "rs544-200g")
        q.set_loopbacks(ports, peer)
        print(f"  200G links {links_200(bar, ports, args, 'prime ' + str(plen))}", flush=True)
        probe(ports, args, "200G traffic")
        one = argparse.Namespace(**vars(args))
        one.len = plen
        q.configure(ports, peer, one, count=1)
        q.clear(ports)
        for p in ports.values():
            p.wr(q.G_CTRL, 1)
        q.wait_idle(ports)
        time.sleep(0.05)
        got = {n: p.rd64(q.RX_FRAMES) for n, p in ports.items()}
        print(f"  one {plen} B frame at 200G, received {got}", flush=True)
        if any(v != 1 for v in got.values()):
            print(f"  prime {plen}: not every port got its frame; that port's 100G result says nothing", flush=True)
        for dc in (0, 1):
            q.set_speed(bar, dc, 100, "rs528")
        q.set_loopbacks(ports, peer)
        wait_links(ports)
        bad = probe(ports, args, f"100G after {plen} B")
        tally.setdefault(plen, []).append(4 - len(bad))
    for plen, v in tally.items():
        print(f"last 200G frame {plen} B: clean ports at 100G {v}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="0000:1b:00")
    ap.add_argument("--len", type=int, default=1496)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("resets").set_defaults(fn=cmd_resets)
    s = sub.add_parser("cycle")
    s.add_argument("n", type=int)
    s.set_defaults(fn=cmd_cycle)
    s = sub.add_parser("prime")
    s.add_argument("lens", type=int, nargs="+")
    s.set_defaults(fn=cmd_prime)
    args = ap.parse_args()
    args.len_max, args.mask, args.gap, args.count, args.seed = 0, 0, 0, 0, 0x9E3779B97F4A7C15
    args.tries, args.wait = 6, 3.0                      # for q.bring_up
    return args.fn(q.Bar(args.device), args)


if __name__ == "__main__":
    sys.exit(main())
