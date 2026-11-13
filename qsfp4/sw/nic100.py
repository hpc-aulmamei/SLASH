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

"""One V80 QSFP port against an external NIC (e.g. a ConnectX-5) over a cable.
The NIC does not echo, so each direction is checked on its own: the V80 tester
sends and the NIC's ethtool counters receive; the NIC sends (pktgen) and the
DCMAC RX counters receive. The DCMAC of the port must already be at the NIC's
rate (qsfp_loop.py speed 100 --dcmac D).

  nic100.py -d 0000:1b:00 up P                 loopback off, link up (GT RX resets if needed)
  nic100.py -d 0000:1b:00 tx P --seconds 10    tester sends at line rate to the NIC
  nic100.py -d 0000:1b:00 rx P --cmd "..."     DCMAC RX counts while a command runs (pktgen)
"""
import argparse
import subprocess
import sys
import time

import qsfp_loop as q


def port(bar, n):
    p = q.Port(bar, n)
    q.stop({n: p})
    return p


def show(p):
    q.phy_show(p)


def cmd_up(bar, args):
    p = port(bar, args.port)
    p.loopback(0)
    t0 = time.time()
    t_end = t0 + args.timeout
    last_reset = t0
    while time.time() < t_end:
        if p.stable(samples=10, interval=0.05):
            print(f"QSFP{args.port}: link up after {time.time() - t0:.1f} s")
            show(p)
            return 0
        if time.time() - last_reset > 3.0:
            print(f"QSFP{args.port}: link down (rx phy {p.link()[1]:#05x}), GT RX + DCMAC RX reset", flush=True)
            p.rx_reset()
            last_reset = time.time()
        time.sleep(0.1)
    print(f"QSFP{args.port}: link DOWN after {args.timeout:.0f} s")
    show(p)
    return 1


def total(acc, c):
    for k, v in c.items():
        acc[k] = acc.get(k, 0) + v


def report(n, c, secs):
    print(f"QSFP{n} DCMAC over {secs:.1f} s:")
    print(f"  tx: frames {c['tx_total_packets']} good {c['tx_good_packets']} bytes {c['tx_total_bytes']} "
          f"bad_fcs {c['tx_bad_fcs']} frame_error {c['tx_frame_error']}  "
          f"{c['tx_total_bytes'] * 8 / secs / 1e9:.2f} Gb/s with FCS")
    print(f"  rx: frames {c['total_packets']} good {c['good_packets']} bytes {c['total_bytes']} "
          f"bad_fcs {c['bad_fcs']} packet_bad_fcs {c['packet_bad_fcs']} bad_code {c['bad_code']} "
          f"stomped {c['stomped_fcs']} truncated {c['truncated']} small {c['small']} toolong {c['toolong']} "
          f"jabber {c['jabber']} oversize {c['oversize']}  {c['total_bytes'] * 8 / secs / 1e9:.2f} Gb/s with FCS")
    print(f"  fec: codewords {c['fec_cw']} corrected {c['fec_corrected']} uncorrected {c['fec_uncorrected']}")


def link_ok(p, n):
    up, rx, fec, _ = p.link()
    print(f"QSFP{n}: link {'up' if up else 'DOWN'}, rx phy {rx:#05x}, rx fec {fec:#04x}")
    return up


def cmd_tx(bar, args):
    n = args.port
    p = port(bar, n)
    if not link_ok(p, n):
        return 1
    ports = {n: p}
    a = argparse.Namespace(len=args.len, len_max=0, mask=0, gap=args.gap, count=0, seed=0x9E3779B97F4A7C15)
    q.configure(ports, {n: n}, a)
    p.wr64(q.G_DST, int(args.dst.replace(":", ""), 16))
    q.clear(ports)
    p.counters()                                   # empty the DCMAC counters
    acc = {}
    try:
        p.wr(q.G_CTRL, 1)
        t0 = last_t = time.monotonic()
        last_b = p.rd64(q.TX_BYTES)
        while time.monotonic() - t0 < args.seconds:
            time.sleep(min(args.interval, args.seconds - (time.monotonic() - t0)))
            now, b = time.monotonic(), p.rd64(q.TX_BYTES)
            total(acc, p.counters())
            print(f"  {now - t0:5.1f}s tester tx {(b - last_b) * 8 / (now - last_t) / 1e9:6.2f} Gb/s "
                  f"(no FCS), link {'up' if p.link()[0] else 'DOWN'}", flush=True)
            last_t, last_b = now, b
    finally:
        q.stop(ports)
    secs = time.monotonic() - t0
    time.sleep(0.2)
    total(acc, p.counters())
    s = q.snapshot(p)
    print(f"QSFP{n} tester: sent {s['tx_frames']} frames of {args.len} B + 4 B FCS to {args.dst}, "
          f"{s['tx_bytes']} B without FCS ({s['tx_frames'] * (args.len + 4)} B with FCS)")
    report(n, acc, secs)
    ok = acc["tx_total_packets"] >= s["tx_frames"] and not acc["tx_bad_fcs"] and not acc["tx_frame_error"]
    print("compare with the NIC: rx_packets_phy delta should be", s["tx_frames"],
          "(+ nothing else: the V80 sends only these)")
    return 0 if ok else 1


def cmd_rx(bar, args):
    n = args.port
    p = port(bar, n)
    if not link_ok(p, n):
        return 1
    p.counters()
    t0 = time.monotonic()
    if args.cmd:
        rc = subprocess.call(args.cmd, shell=True)
        if rc:
            print(f"command exited {rc}")
    else:
        time.sleep(args.seconds)
    time.sleep(0.5)                                # last frames in flight
    secs = time.monotonic() - t0
    c = p.counters()
    report(n, c, secs)
    up = link_ok(p, n)
    bad = c["bad_fcs"] + c["packet_bad_fcs"] + c["bad_code"] + c["stomped_fcs"] + c["truncated"] + c["fec_uncorrected"]
    print(f"compare with the NIC: tx_packets_phy delta should be {c['total_packets']} (pktgen + the host's own frames)")
    return 0 if up and not bad and c["total_packets"] else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="0000:1b:00")
    sub = ap.add_subparsers(dest="cmd_name", required=True)
    s = sub.add_parser("up")
    s.add_argument("port", type=int, choices=sorted(q.PORTS))
    s.add_argument("--timeout", type=float, default=15.0)
    s.set_defaults(fn=cmd_up)
    s = sub.add_parser("tx")
    s.add_argument("port", type=int, choices=sorted(q.PORTS))
    s.add_argument("--seconds", type=float, default=10.0)
    s.add_argument("--interval", type=float, default=2.0)
    s.add_argument("--len", type=int, default=1496, help="frame bytes without FCS")
    s.add_argument("--gap", type=int, default=0)
    s.add_argument("--dst", default="02:00:00:00:00:99", help="destination MAC (default: not the NIC's, "
                   "so the NIC counts the frames at its PHY and drops them before the host)")
    s.set_defaults(fn=cmd_tx)
    s = sub.add_parser("rx")
    s.add_argument("port", type=int, choices=sorted(q.PORTS))
    s.add_argument("--seconds", type=float, default=10.0, help="without --cmd")
    s.add_argument("--cmd", help="shell command that makes the NIC send (runs as root)")
    s.set_defaults(fn=cmd_rx)
    args = ap.parse_args()
    return args.fn(q.Bar(args.device), args)


if __name__ == "__main__":
    sys.exit(main())
