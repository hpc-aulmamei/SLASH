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

"""Host control of the four-port V80 Ethernet test over PF2 BAR2.

Ports are SLASH numbers, QSFPn, in DS1013 cage n + 1. A test is a set of paths:
--pair A-B  a cable between two ports, frames both ways
--loop P    port P in GT near-end PMA loopback, frames to itself

  sudo ./qsfp_loop.py -d 0000:1b:00 id                 # all four testers answer
  sudo ./qsfp_loop.py -d 0000:1b:00 phy                # GT and reset status of each port
  sudo ./qsfp_loop.py -d 0000:1b:00 phy --port 1 --loopback 2
  sudo ./qsfp_loop.py -d 0000:1b:00 restart --dcmac 0  # rerun DCMAC0's whole reset sequence
  sudo ./qsfp_loop.py -d 0000:1b:00 restart --port 1   # GT datapaths and DCMAC port of QSFP1 only
  sudo ./qsfp_loop.py -d 0000:1b:00 restart --port 1 --serdes   # DCMAC TX serdes of QSFP1 only
  sudo ./qsfp_loop.py -d 0000:1b:00 speed 100 --dcmac 0  # cages 1 and 2 at 100CAUI-4 (QSFP_DYNAMIC builds)
  sudo ./qsfp_loop.py -d 0000:1b:00 up --pair 0-2 --loop 1 --loop 3
  sudo ./qsfp_loop.py -d 0000:1b:00 run --pair 0-2 --loop 1 --loop 3 --seconds 600
  sudo ./qsfp_loop.py -d 0000:1b:00 dcmac              # DCMAC counters since the last tick
  sudo ./qsfp_loop.py -d 0000:1b:00 fault              # first error latched by each checker
  sudo ./qsfp_loop.py -d 0000:1b:00 lengths --pair 0-2 --loop 1 --loop 3

run prints one line per interval and a table per cage at the end, and exits 0
only if every port passed. Link health is decided from traffic: `up` sends frames
after the status bits say up. Register map: qsfp4/README.md.
"""
import argparse
import json
import mmap
import os
import sys
import time

BAR2_SIZE = 128 << 20
CLK_HZ = 230e6                  # eth_test clock: DCMAC clock wizard VCO 3910 MHz / 17
TEST = 0x0400_0000              # eth_test of QSFPn at TEST + n * 0x1_0000
TEST_ID = 0x45544854            # "ETHT"
DCMAC = (0x0200_0000, 0x0300_0000)
# QSFPn -> (DCMAC, DCMAC port). DCMAC1 as the IP places it on DCMAC_X0Y2.
PORTS = {0: (0, 0), 1: (0, 2), 2: (1, 2), 3: (1, 0)}
QUAD = {0: "GTM_QUAD_X1Y7", 1: "GTM_QUAD_X1Y8", 2: "GTM_QUAD_X0Y10", 3: "GTM_QUAD_X0Y9"}

# eth_test registers
ID, VERSION, SCRATCH, BUILD, CYCLES, PORT = 0x000, 0x004, 0x008, 0x00C, 0x010, 0x018
PHY_CTRL, PHY_STATUS, RX_DROPS = 0x020, 0x024, 0x028
G_CTRL, LEN_MIN, LEN_MAX, LEN_MASK, GAP, ETYPE, G_COUNT = 0x040, 0x044, 0x048, 0x04C, 0x050, 0x054, 0x058
G_DST, G_SRC, SEED = 0x060, 0x068, 0x070
TX_FRAMES, TX_BYTES = 0x080, 0x088
C_CTRL, C_DST, C_SRC, C_ID = 0x100, 0x108, 0x110, 0x118
RX_FRAMES, RX_BYTES, LOST = 0x120, 0x128, 0x130
REORDER, HDR_ERR, LEN_ERR, PAY_ERR, LAST_ID, LAST_SRC = 0x138, 0x13C, 0x140, 0x144, 0x148, 0x14C
FIRST, FIRST_SEQ, FIRST_EXP, FIRST_GOT = 0x154, 0x158, 0x160, 0x168

LOOPBACK = {0: "off", 1: "near-end PCS", 2: "near-end PMA", 4: "far-end PMA", 6: "far-end PCS"}
PHY_BITS = ["gt_tx_reset_done", "gt_rx_reset_done", "gt_powergood", "dcmac_mmcm_locked",
            "dcmac_tx_core_reset", "dcmac_rx_core_reset", "dcmac_rx_serdes_reset", "axis_out_of_reset",
            "sequencer_held", "rate_100g"]
RX_PHY_OK = 0x005               # status and aligned (0x005 at 200G; 100G also sets block lock and synced: 0x107)
RX_PHY_BAD = 0xEF8              # misaligned, aligned_err, hi_ber, framing, bad code, synced_err, bip, flex fifo
RX_FEC_OK = 0x3D

# DCMAC per-port registers, as offsets in the port's block (0x1000 * (port + 1))
D_TX_MODE, D_RX_MODE = 0x040, 0x044
D_TICK_RX, D_TICK_TX = 0x0F4, 0x0FC
D_CTL_RX, D_CTL_TX = 0x0F0, 0x0F8
D_TX_MAC_RT = 0x104
D_RX_STATS, D_TX_STATS = 0x400, 0x200
D_TX_READY, D_RX_PHY, D_RX_PHY_RT, D_RX_READY = 0x808, 0xC00, 0xC04, 0xC08
D_BLOCK_LOCK, D_LANE_SYNC, D_LANE_SYNC_ERR = 0xC0C, 0xC10, 0xC14
D_RX_FEC, D_RX_FEC_RT = 0xC34, 0xC38
D_FEC_CW, D_FEC_CORR, D_FEC_UNCORR = 0xE48, 0xE50, 0xE58      # PG369, 47 bits, per tick
D_FLUSH_RX, D_FLUSH_TX = 0x030, 0x038   # CHANNEL_CONTROL_REG_RX/TX: [0] channel flush

# Runtime rate (builds with QSFP_DYNAMIC, PHY_CTRL[8] of DCMAC port 0's tester):
# per-port TX_MODE / RX_MODE fields as AMD's DCMAC Dynamic Configuration example
RATE = {100: 0, 200: 1}                 # ctl_tx/rx_data_rate
FEC_MODE = {"rs528": 0b00101, "rs544": 0b00100, "rs544-200g": 0b01000, "none": 0b00000}
SPEED_BIT = 8
DCMAC_RX = [("total_packets", 0x10), ("good_packets", 0x18), ("total_bytes", 0x00), ("small", 0x20),
            ("bad_code", 0x28), ("bad_fcs", 0x30), ("packet_bad_fcs", 0x38), ("stomped_fcs", 0x40),
            ("truncated", 0x48), ("toolong", 0xB0), ("jabber", 0xC0), ("oversize", 0xC8)]
DCMAC_TX = [("total_packets", 0x10), ("good_packets", 0x18), ("total_bytes", 0x00),
            ("frame_error", 0x20), ("bad_fcs", 0x28)]
FEC = [("fec_cw", D_FEC_CW), ("fec_corrected", D_FEC_CORR), ("fec_uncorrected", D_FEC_UNCORR)]


class Bar:
    """PF2 BAR2. Whole 32-bit accesses only."""

    def __init__(self, bdf):
        path = f"/sys/bus/pci/devices/{bdf}.2/resource2"
        self.fd = os.open(path, os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(self.fd, BAR2_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        self.mv = memoryview(self.mm).cast("I")

    def rd(self, off):
        return self.mv[off >> 2]

    def wr(self, off, val):
        self.mv[off >> 2] = val & 0xFFFF_FFFF


def rd64(rd, off):
    """HI, LO, HI, until the two HI reads agree. A counter that keeps carrying
    into HI (the lost count when garbage sequence numbers arrive) never
    settles: then the last reading, approximate, rather than an exception that
    leaves the generators running."""
    for _ in range(8):
        hi = rd(off + 4)
        lo = rd(off)
        if rd(off + 4) == hi:
            return hi << 32 | lo
    return hi << 32 | lo


class Port:
    def __init__(self, bar, n):
        self.bar, self.n = bar, n
        self.base = TEST + n * 0x1_0000
        self.dc, self.dport = PORTS[n]
        self.d = DCMAC[self.dc]
        self.block = self.d + 0x1000 * (self.dport + 1)

    def rd(self, off):
        return self.bar.rd(self.base + off)

    def wr(self, off, val):
        self.bar.wr(self.base + off, val)

    def rd64(self, off):
        return rd64(self.rd, off)

    def wr64(self, off, val):
        self.wr(off, val)
        self.wr(off + 4, val >> 32)

    def mac_rd(self, off):
        return self.bar.rd(self.block + off)

    def mac_wr(self, off, val):
        self.bar.wr(self.block + off, val)

    def present(self):
        return self.rd(ID) == TEST_ID and self.rd(PORT) == self.n

    def link(self):
        """(up, rx phy status now, rx fec status now, tx mac status now)"""
        rx = self.mac_rd(D_RX_PHY_RT)
        tx = self.mac_rd(D_TX_MAC_RT)
        return bool(rx & 1 and rx & 4 and not tx & 1), rx, self.mac_rd(D_RX_FEC_RT), tx

    def stable(self, samples=5, interval=0.02):
        for _ in range(samples):
            if not self.link()[0]:
                return False
            time.sleep(interval)
        return True

    def pulse(self, bit):
        ctrl = self.rd(PHY_CTRL) & ~0xF0
        self.wr(PHY_CTRL, ctrl | 1 << bit)
        time.sleep(0.01)
        self.wr(PHY_CTRL, ctrl)

    def loopback(self, mode):
        self.wr(PHY_CTRL, (self.rd(PHY_CTRL) & ~7) | mode)

    def rx_reset(self):
        """GT RX datapath of this quad, then the DCMAC RX of this port (the two
        channels of a 200G port), as AMD's example; the far end is not touched."""
        self.pulse(4)
        time.sleep(0.05)
        blocks = [self.block, self.block + 0x1000]
        for b in blocks:
            self.bar.wr(b + D_CTL_RX, 2)
        time.sleep(0.002)
        for b in blocks:
            self.bar.wr(b + D_CTL_RX, 0)

    def wait_status(self, bit, timeout=2.0):
        """Until PHY_STATUS[bit] is 1. After a reset pulse the done bit first
        has to drop (CDC delay), so look for 0 before waiting for 1."""
        t_end = time.time() + 0.05
        while time.time() < t_end and self.rd(PHY_STATUS) >> bit & 1:
            time.sleep(0.001)
        t_end = time.time() + timeout
        while time.time() < t_end:
            if self.rd(PHY_STATUS) >> bit & 1:
                return True
            time.sleep(0.005)
        return False

    def tx_serdes_hold(self, on):
        """DCMAC soft TX serdes reset (PORT_CONTROL_REG_TX bit 1) of the two
        channels of this 200G port; the other bits (TX flexif reset) are kept."""
        for b in (self.block, self.block + 0x1000):
            v = self.bar.rd(b + D_CTL_TX)
            self.bar.wr(b + D_CTL_TX, v | 2 if on else v & ~2)

    def tx_serdes_reset(self):
        """DCMAC TX serdes side of this port only, GT untouched."""
        self.tx_serdes_hold(True)
        time.sleep(0.002)
        self.tx_serdes_hold(False)

    def tx_reset(self):
        """GT TX datapath of this quad, in PG369's TX startup order: the DCMAC
        TX serdes of this port held in reset until the GT TX is done again.
        A GT TX reset alone stops the TX serdes clock under a running DCMAC
        port, and can leave that port sending garbage until reconfiguration."""
        self.tx_serdes_hold(True)
        self.pulse(6)
        ok = self.wait_status(0)
        time.sleep(0.001)
        self.tx_serdes_hold(False)
        return ok

    def tick(self):
        """Latch this port's counters since the previous tick (and clear them)."""
        for b in (self.block, self.block + 0x1000):
            self.bar.wr(b + D_TX_MODE, self.bar.rd(b + D_TX_MODE) | 1 << 4)
            self.bar.wr(b + D_RX_MODE, self.bar.rd(b + D_RX_MODE) | 1 << 11)
            for reg in (D_TICK_TX, D_TICK_RX):
                self.bar.wr(b + reg, 0)
                self.bar.wr(b + reg, 1)
        for _ in range(10):
            if self.mac_rd(D_RX_READY) and self.mac_rd(D_TX_READY):
                break
            time.sleep(0.001)

    def counters(self):
        """Tick, then the counts of the interval. FEC of this port and the next
        FEC port and their BH halves: a 200G port may count on more than one."""
        self.tick()
        rd = self.mac_rd
        out = {k: rd64(rd, D_RX_STATS + o) for k, o in DCMAC_RX}
        out.update({"tx_" + k: rd64(rd, D_TX_STATS + o) for k, o in DCMAC_TX})
        nxt = lambda o: self.bar.rd(self.block + 0x1000 + o)
        for k, o in FEC:
            out[k] = rd64(rd, o) + rd64(rd, o + 0x18) + rd64(nxt, o) + rd64(nxt, o + 0x18)
        return out


def macs(n):
    return 0x0200_0000_0000 | n


def parse_paths(args):
    """{sender: receiver} for every port in the test."""
    peer = {}
    for p in args.pair or []:
        a, b = (int(x) for x in p.split("-"))
        peer[a], peer[b] = b, a
    for p in args.loop or []:
        peer[p] = p
    if not peer or any(p not in PORTS for p in peer) or len(set(peer.values())) != len(peer):
        sys.exit("give --pair A-B / --loop P, each port 0..3 once")
    return peer


def set_loopbacks(ports, peer):
    for n, p in ports.items():
        p.loopback(2 if peer[n] == n else 0)


def stop(ports):
    """Generators off, also ones left running by an interrupted run."""
    for p in ports.values():
        p.wr(G_CTRL, 2)
    return wait_idle(ports)


def configure(ports, peer, args, count=None):
    stop(ports)
    for n, p in ports.items():
        far = peer[n]                        # we send to far; far sends to peer[far] == us
        p.wr(LEN_MIN, args.len)
        p.wr(LEN_MAX, args.len_max or args.len)
        p.wr(LEN_MASK, args.mask)
        p.wr(GAP, args.gap)
        p.wr64(G_COUNT, args.count if count is None else count)
        p.wr64(G_DST, macs(far))
        p.wr64(G_SRC, macs(n))
        p.wr64(C_DST, macs(n))
        p.wr64(C_SRC, macs(far))
        p.wr(C_ID, far)
        p.wr64(SEED, args.seed)               # shared: each checker recomputes its sender's pattern


def clear(ports):
    for p in ports.values():
        p.wr(G_CTRL, 4)
        p.wr(C_CTRL, 1)


def wait_idle(ports, timeout=5.0):
    t_end = time.time() + timeout
    while time.time() < t_end:
        if not any(p.rd(G_CTRL) & 1 for p in ports.values()):
            return True
        time.sleep(0.01)
    return False


def drop_delta(now, base):
    """RX_DROPS is free running and not cleared: [11:0] bad FCS, [15:12] overflow."""
    return (now - base) & 0xFFF, ((now >> 12) - (base >> 12)) & 0xF


def snapshot(p):
    return {"tx_frames": p.rd64(TX_FRAMES), "tx_bytes": p.rd64(TX_BYTES),
            "rx_frames": p.rd64(RX_FRAMES), "rx_bytes": p.rd64(RX_BYTES),
            "lost": p.rd64(LOST), "reorder": p.rd(REORDER), "hdr_err": p.rd(HDR_ERR),
            "len_err": p.rd(LEN_ERR), "pay_err": p.rd(PAY_ERR), "drops": p.rd(RX_DROPS),
            "last_id": p.rd(LAST_ID)}


def probe(ports, peer, args, frames=2000):
    """Send frames on every path; the ports that did not get them all clean."""
    configure(ports, peer, args, count=frames)
    clear(ports)
    base = {n: p.rd(RX_DROPS) for n, p in ports.items()}
    for p in ports.values():
        p.wr(G_CTRL, 1)
    wait_idle(ports)
    time.sleep(0.05)
    bad = {}
    for n, p in ports.items():
        s = snapshot(p)
        fcs, ovf = drop_delta(s["drops"], base[n])
        if (s["rx_frames"] != frames or s["lost"] or s["hdr_err"] or s["len_err"] or s["pay_err"]
                or s["reorder"] or fcs or ovf or s["last_id"] != peer[n]):
            bad[n] = (f"rx {s['rx_frames']}/{frames} lost {s['lost']} hdr {s['hdr_err']} len {s['len_err']} "
                      f"payload {s['pay_err']} from port {s['last_id']} (expected {peer[n]}) "
                      f"bad FCS drops {fcs} overflow drops {ovf}")
    return bad


def bring_up(bar, ports, peer, args):
    """Links up and frames clean. For a port that fails, each try (--recover gt,
    the default): the GT TX datapath reset of its sender (the far end, or itself in
    loopback) with the sender's DCMAC TX serdes held, then its own GT RX and DCMAC
    RX reset. A failing port is often FEC lane-locked but not aligned (rx fec 0x3c);
    the DCMAC reset sequence (--recover mixed, the old default) does not clear that
    and puts working ports into the same state. Measured 2026-11-15 on the cabled
    QSFP0-QSFP2 pair after a rate switch, 20 rounds each: gt up within 3 tries
    every time (13 at the first), serdes within 3, mixed 1 to 4 failures in 20."""
    t0 = time.time()
    set_loopbacks(ports, peer)
    time.sleep(0.05)
    for attempt in range(args.tries + 1):
        down = [n for n, p in ports.items() if not p.stable()]
        why = {n: "link down" for n in down}
        if not down:
            bad = probe(ports, peer, args)
            down, why = list(bad), bad
        if not down:
            print(f"all links up and clean after {time.time() - t0:.2f} s", flush=True)
            return True
        if attempt == args.tries:
            break
        for n in down:
            print(f"QSFP{n}: {why[n]}", flush=True)
        if args.recover == "gt":
            # the sender's GT TX datapath (its DCMAC TX serdes held), then the RX
            senders = sorted({peer[n] for n in down})
            print(f"try {attempt + 1}/{args.tries}: GT TX reset of {senders}, RX reset of {down}",
                  flush=True)
            for n in senders:
                if not ports[n].tx_reset():
                    print(f"QSFP{n}: GT TX reset not done after 2 s", flush=True)
            for n in down:
                ports[n].rx_reset()
        elif args.recover == "serdes" or attempt % 2 == 0:
            # the sender may be the broken side: its DCMAC TX serdes too
            senders = sorted({peer[n] for n in down})
            print(f"try {attempt + 1}/{args.tries}: TX serdes reset of {senders}, RX reset of {down}",
                  flush=True)
            for n in senders:
                ports[n].tx_serdes_reset()
            for n in down:
                ports[n].rx_reset()
        else:
            dcs = sorted({ports[n].dc for n in down})
            print(f"try {attempt + 1}/{args.tries}: reset sequence of DCMAC {dcs}", flush=True)
            for dc in dcs:
                restart_dcmac(bar, dc)
            time.sleep(1.5)
            for p in ports.values():
                p.rx_reset()
        t_end = time.time() + args.wait
        while time.time() < t_end and not all(p.link()[0] for p in ports.values()):
            time.sleep(0.05)
    for n in ports:
        phy_show(ports[n])
    return False


def restart_dcmac(bar, dc):
    """Both ports' sequencers hold it; bit 7 of either reruns it.
    dcmac_reset_ctrl (Versal-DCMAC) latches the GT reset-done from before the
    rerun, so it resets and releases the DCMAC within microseconds while the GT
    quads take ~0.25 s. Once both quads are done and the sequencer has finished
    (AXIS out of reset), reset each port's DCMAC TX serdes side again."""
    ns = [k for k, v in PORTS.items() if v[0] == dc]
    Port(bar, ns[0]).pulse(7)
    for n in ns:
        p = Port(bar, n)
        p.wait_status(0)
        p.wait_status(1)
    t_end = time.time() + 1.0
    while time.time() < t_end and not Port(bar, ns[0]).rd(PHY_STATUS) >> 7 & 1:
        time.sleep(0.005)
    for n in ns:
        Port(bar, n).tx_serdes_reset()


def dcmac_mode(rate, fec):
    """(TX_MODE, RX_MODE) of a port: data rate, tick by register, PMA lane mux
    (1 for PAM4 200GAUI-4, 0 for NRZ 100CAUI-4), FEC mode, four-lane PMD at 100G."""
    lane_mux = 1 if rate == 200 else 0
    # ctl_tx_fec_four_lane_pmd (TX_MODE[22], TX only): RS-FEC framing for a four-lane
    # PMD (100GBASE-SR4/CR4/KR4). Without it the V80 talks to itself (its RX takes
    # both framings) but a ConnectX-5 never finds a partner; found on hardware 2026-11-11.
    four_lane = int(rate == 100 and fec.startswith("rs"))
    tx = RATE[rate] | 1 << 4 | lane_mux << 9 | FEC_MODE[fec] << 16 | four_lane << 22
    rx = RATE[rate] | 1 << 11 | lane_mux << 12 | FEC_MODE[fec] << 16
    return tx, rx


def set_speed(bar, dc, rate, fec):
    """Both ports of DCMAC dc to rate (100 or 200), in the order of AMD's dynamic
    example: flush and reset the channels, switch the GT line rate and DCMAC
    configuration (PHY_CTRL[8]), wait for both quads, reset the DCMAC, write the
    port modes, release. Links come up with `up` afterwards."""
    d = DCMAC[dc]
    ns = sorted(k for k, v in PORTS.items() if v[0] == dc)
    ctl = Port(bar, [k for k in ns if PORTS[k][1] == 0][0])
    for n in ns:
        b = d + 0x1000 * (PORTS[n][1] + 1)
        print(f"QSFP{n}: DCMAC{dc} port {PORTS[n][1]} before: TX_MODE {bar.rd(b + D_TX_MODE):#010x} "
              f"RX_MODE {bar.rd(b + D_RX_MODE):#010x}")
    chans = [d + 0x1000 * (c + 1) for c in range(6)]
    for b in chans[:4]:                                  # ports 0 and 2, two channels each
        bar.wr(b + D_FLUSH_RX, 1)
        bar.wr(b + D_FLUSH_TX, 1)
    time.sleep(0.001)
    for b in chans[:4]:
        bar.wr(b + D_CTL_RX, 3)
        bar.wr(b + D_CTL_TX, 3)
    v = ctl.rd(PHY_CTRL) & ~(1 << SPEED_BIT)
    ctl.wr(PHY_CTRL, v | (rate == 100) << SPEED_BIT)
    for n in ns:                                          # GT rate change: both quads reset
        p = Port(bar, n)
        if not (p.wait_status(0) and p.wait_status(1)):
            print(f"QSFP{n}: GT reset not done after the rate change")
    if (ctl.rd(PHY_STATUS) >> 9 & 1) != (rate == 100):
        print(f"DCMAC{dc}: PHY_STATUS rate bit does not follow PHY_CTRL; is this a QSFP_DYNAMIC build?")
    for b in [d] + chans:                                 # all DCMAC resets
        bar.wr(b + D_CTL_RX, 0xFFFF_FFFF)
        bar.wr(b + D_CTL_TX, 0xFFFF_FFFF)
    if rate == 100:                                       # AMD ETHIP-4113: down to 100G on all first
        tx, rx = dcmac_mode(100, fec)
        for b in chans:
            bar.wr(b + D_TX_MODE, tx)
            bar.wr(b + D_RX_MODE, rx)
    tx, rx = dcmac_mode(rate, fec)
    for n in ns:
        b = d + 0x1000 * (PORTS[n][1] + 1)
        bar.wr(b + D_TX_MODE, tx)
        bar.wr(b + D_RX_MODE, rx)
    for b in [d] + chans:
        bar.wr(b + D_CTL_RX, 0)
        bar.wr(b + D_CTL_TX, 0)
    time.sleep(0.001)
    for b in chans[:4]:
        bar.wr(b + D_FLUSH_RX, 0)
        bar.wr(b + D_FLUSH_TX, 0)
    for n in ns:
        b = d + 0x1000 * (PORTS[n][1] + 1)
        print(f"QSFP{n}: DCMAC{dc} port {PORTS[n][1]} TX_MODE {bar.rd(b + D_TX_MODE):#010x} "
              f"RX_MODE {bar.rd(b + D_RX_MODE):#010x}")


def phy_show(p):
    up, rx, fec, tx = p.link()
    ctrl, st = p.rd(PHY_CTRL), p.rd(PHY_STATUS)
    print(f"QSFP{p.n} (cage {p.n + 1}, {QUAD[p.n]}, DCMAC{p.dc} port {p.dport}): link {'up' if up else 'DOWN'}, "
          f"rx phy {rx:#05x}, rx fec {fec:#04x}, tx mac {tx:#04x}, block lock {p.mac_rd(D_BLOCK_LOCK):#x}, "
          f"lane sync {p.mac_rd(D_LANE_SYNC):#x}")
    print(f"  PHY_CTRL {ctrl:#06x}: loopback {LOOPBACK.get(ctrl & 7, ctrl & 7)}, cdr hold {ctrl >> 3 & 1}")
    print(f"  PHY_STATUS {st:#06x}: " + " ".join(f"{b}={st >> i & 1}" for i, b in enumerate(PHY_BITS)))


def all_ports(bar):
    return {n: Port(bar, n) for n in PORTS}


def cmd_id(bar, args):
    rc = 0
    for n, p in all_ports(bar).items():
        ok = p.present()
        p.wr(SCRATCH, 0x5A5A0000 | n)
        sc = p.rd(SCRATCH) == (0x5A5A0000 | n)
        print(f"QSFP{n}: id {p.rd(ID):#010x} version {p.rd(VERSION):#010x} build {p.rd(BUILD)} port {p.rd(PORT)} "
              f"scratch {'ok' if sc else 'BAD'} {'ok' if ok else 'BAD'}")
        rc |= 0 if ok and sc else 1
    return rc


def cmd_phy(bar, args):
    ports = all_ports(bar)
    for n in ([args.port] if args.port is not None else ports):
        if args.loopback is not None:
            ports[n].loopback(args.loopback)
    if args.loopback is not None:
        time.sleep(args.wait)
    for p in ports.values():
        phy_show(p)
    return 0


def cmd_restart(bar, args):
    if args.dcmac is not None:
        restart_dcmac(bar, args.dcmac)
    else:
        p = Port(bar, args.port)
        if args.serdes:
            p.tx_serdes_reset()
        else:
            if not p.tx_reset():             # GT TX datapath, DCMAC TX serdes held
                print(f"QSFP{args.port}: GT TX reset not done after 2 s")
            p.rx_reset()
    time.sleep(args.wait)
    for p in all_ports(bar).values():
        phy_show(p)
    return 0


def cmd_speed(bar, args):
    fec = args.fec or ("rs528" if args.rate == 100 else "rs544-200g")
    for dc in ([args.dcmac] if args.dcmac is not None else (0, 1)):
        set_speed(bar, dc, args.rate, fec)
    time.sleep(args.wait)
    for p in all_ports(bar).values():
        phy_show(p)
    return 0


def cmd_up(bar, args):
    peer = parse_paths(args)
    ports = {n: Port(bar, n) for n in peer}
    return 0 if bring_up(bar, ports, peer, args) else 1


def cmd_dcmac(bar, args):
    for n, p in all_ports(bar).items():
        c = p.counters()
        print(f"QSFP{n}: " + " ".join(f"{k} {v}" for k, v in c.items()))
        print(f"  rx phy latched {p.mac_rd(D_RX_PHY):#06x} now {p.mac_rd(D_RX_PHY_RT):#06x}, "
              f"rx fec latched {p.mac_rd(D_RX_FEC):#04x} now {p.mac_rd(D_RX_FEC_RT):#04x}, "
              f"lane sync err {p.mac_rd(D_LANE_SYNC_ERR):#06x}")
    return 0


def cmd_fault(bar, args):
    kinds = ["payload", "header", "length"]
    for n, p in all_ports(bar).items():
        f = p.rd(FIRST)
        if not f >> 31:
            print(f"QSFP{n}: no error latched")
            continue
        names = "+".join(k for i, k in enumerate(kinds) if f >> (16 + i) & 1)
        print(f"QSFP{n}: first error {names}, lane {f >> 20 & 15}, beat {f & 0xFFFF}, seq {p.rd64(FIRST_SEQ)}, "
              f"expected {p.rd64(FIRST_EXP):#018x}, received {p.rd64(FIRST_GOT):#018x}")
    return 0


def run(bar, ports, peer, args, seconds, label=""):
    """One soak. Returns (passed, rows)."""
    configure(ports, peer, args)
    clear(ports)
    base = {n: p.rd(RX_DROPS) for n, p in ports.items()}
    acc = {n: {} for n in ports}

    def tick_all():
        for n, p in ports.items():
            for k, v in p.counters().items():
                acc[n][k] = acc[n].get(k, 0) + v

    tick_all()                               # empties the counters before the run
    acc = {n: {} for n in ports}
    for p in ports.values():
        p.wr(G_CTRL, 1)
    t0 = last_t = time.monotonic()
    last = {n: snapshot(p) for n, p in ports.items()}
    worst = {n: float("inf") for n in ports}
    downs = {n: 0 for n in ports}
    t_end = t0 + seconds
    while True:
        now = time.monotonic()
        if now >= t_end:
            break
        time.sleep(min(args.interval, t_end - now))
        now = time.monotonic()
        dt = now - last_t
        tick_all()
        line = [f"{now - t0:7.1f}s"]
        for n, p in ports.items():
            s = snapshot(p)
            tx = (s["tx_bytes"] - last[n]["tx_bytes"]) * 8 / dt / 1e9
            rx = (s["rx_bytes"] - last[n]["rx_bytes"]) * 8 / dt / 1e9
            if now - t0 > 2 * args.interval:
                worst[n] = min(worst[n], rx)
            if not p.link()[0]:
                downs[n] += 1
            errs = s["lost"] + s["reorder"] + s["hdr_err"] + s["len_err"] + s["pay_err"]
            line.append(f"Q{n} tx {tx:6.1f} rx {rx:6.1f} err {errs}")
            last[n] = s
        last_t = now
        print(" | ".join(line), flush=True)
    for p in ports.values():
        p.wr(G_CTRL, 2)
    t_stop = time.monotonic()
    idle = wait_idle(ports)
    time.sleep(0.2)                          # drain
    tick_all()
    rows = []
    passed = idle
    for n, p in ports.items():
        s = snapshot(p)
        far = ports[peer[n]]
        ftx = snapshot(far)
        c = acc[n]
        secs = max(t_stop - t0, 1e-9)
        missing = ftx["tx_frames"] - s["rx_frames"]
        fcs, ovf = drop_delta(s["drops"], base[n])
        r = {"port": n, "cage": n + 1, "from_port": s["last_id"], "seconds": round(secs, 1),
             "tx_gbps": s["tx_bytes"] * 8 / secs / 1e9, "rx_gbps": s["rx_bytes"] * 8 / secs / 1e9,
             "tx_frames": s["tx_frames"], "rx_frames": s["rx_frames"], "lost": s["lost"], "missing": missing,
             "reorder": s["reorder"], "pay_err": s["pay_err"], "hdr_err": s["hdr_err"], "len_err": s["len_err"],
             "bad_fcs": c.get("bad_fcs", 0) + c.get("packet_bad_fcs", 0), "bad_code": c.get("bad_code", 0),
             "drop_fcs": fcs, "drop_ovf": ovf,
             "fec_cw": c.get("fec_cw", 0), "fec_corrected": c.get("fec_corrected", 0),
             "fec_uncorrected": c.get("fec_uncorrected", 0), "worst_gbps": worst[n], "link_down_samples": downs[n],
             "rx_phy": p.link()[1], "rx_fec": p.link()[2],
             "tester_tx_bytes": s["tx_bytes"], "tester_rx_bytes": s["rx_bytes"],
             "mac_tx_frames": c.get("tx_total_packets", 0), "mac_tx_bytes": c.get("tx_total_bytes", 0),
             "mac_rx_frames": c.get("total_packets", 0), "mac_rx_bytes": c.get("total_bytes", 0)}
        r["pass"] = bool(s["rx_frames"] and not (missing or s["lost"] or s["reorder"] or s["pay_err"] or s["hdr_err"]
                         or s["len_err"] or r["bad_fcs"] or r["bad_code"] or r["drop_fcs"] or r["drop_ovf"]
                         or r["fec_uncorrected"] or downs[n] or s["last_id"] != peer[n]
                         or r["rx_phy"] & (RX_PHY_OK | RX_PHY_BAD) != RX_PHY_OK or r["rx_fec"] != RX_FEC_OK))
        passed = passed and r["pass"]
        rows.append(r)
    return passed, rows


def table(rows):
    print(f"{'port':6}{'cage':>5}{'from':>5}{'s':>7}{'tx Gb/s':>9}{'rx Gb/s':>9}{'min rx':>8}{'tx frames':>15}"
          f"{'rx frames':>15}{'lost':>6}{'miss':>6}{'pay':>5}{'hdr':>5}{'len':>5}{'badFCS':>7}{'badcode':>8}"
          f"{'FEC corr':>13}{'FEC uncorr':>11}{'phy':>7}{'fec':>6}  result")
    for r in rows:
        print(f"QSFP{r['port']:<2}{r['cage']:>5}{r['from_port']:>5}{r['seconds']:>7.0f}{r['tx_gbps']:>9.2f}{r['rx_gbps']:>9.2f}"
              f"{r['worst_gbps']:>8.1f}{r['tx_frames']:>15}{r['rx_frames']:>15}{r['lost']:>6}{r['missing']:>6}"
              f"{r['pay_err']:>5}{r['hdr_err']:>5}{r['len_err']:>5}{r['bad_fcs']:>7}{r['bad_code']:>8}"
              f"{r['fec_corrected']:>13}{r['fec_uncorrected']:>11}{r['rx_phy']:>#7x}{r['rx_fec']:>#6x}"
              f"  {'PASS' if r['pass'] else 'FAIL'}")


def cmd_run(bar, args):
    peer = parse_paths(args)
    ports = {n: Port(bar, n) for n in peer}
    if not args.no_up and not bring_up(bar, ports, peer, args):
        print("links did not come up")
        return 1
    try:
        passed, rows = run(bar, ports, peer, args, args.seconds)
    finally:
        stop(all_ports(bar))
    table(rows)
    if args.json:
        with open(args.json, "a") as f:
            f.write(json.dumps({"time": time.strftime("%FT%T%z"), "args": vars(args), "rows": rows}, default=str) + "\n")
    if not passed:
        cmd_fault(bar, args)
        for r in rows:        # where the length goes wrong: tester bytes have no FCS, the DCMAC's include it
            avg = lambda b, f: f"{b / f:.1f}" if f else "-"
            print(f"QSFP{r['port']}: tester tx {r['tx_frames']} frames avg {avg(r['tester_tx_bytes'], r['tx_frames'])} B, "
                  f"DCMAC tx {r['mac_tx_frames']} avg {avg(r['mac_tx_bytes'], r['mac_tx_frames'])} B, "
                  f"DCMAC rx {r['mac_rx_frames']} avg {avg(r['mac_rx_bytes'], r['mac_rx_frames'])} B, "
                  f"tester rx {r['rx_frames']} avg {avg(r['tester_rx_bytes'], r['rx_frames'])} B")
    print("PASS" if passed else "FAIL")
    return 0 if passed else 1


def cmd_lengths(bar, args):
    """Short runs over the length range, then random lengths, each ~--seconds."""
    peer = parse_paths(args)
    ports = {n: Port(bar, n) for n in peer}
    if not bring_up(bar, ports, peer, args):
        print("links did not come up")
        return 1
    rc = 0
    for name, ln, mx, mask in (("64 B", 60, 60, 0), ("65 B", 61, 61, 0), ("128 B", 124, 124, 0), ("129 B", 125, 125, 0),
                               ("512 B", 508, 508, 0), ("1500 B", 1496, 1496, 0), ("4096 B", 4092, 4092, 0),
                               ("9000 B", 8996, 8996, 0), ("random 64..9000", 60, 8996, 0x3FFF)):
        args.len, args.len_max, args.mask = ln, mx, mask
        print(f"-- {name} (with FCS)", flush=True)
        passed, rows = run(bar, ports, peer, args, args.seconds)
        table(rows)
        rc |= 0 if passed else 1
    print("PASS" if rc == 0 else "FAIL")
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="0000:1b:00", help="bus:dev.func without the function")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def paths(s):
        s.add_argument("--pair", action="append", help="A-B: a cable between ports A and B")
        s.add_argument("--loop", action="append", type=int, help="P: near-end PMA loopback on port P")
        s.add_argument("--len", type=lambda x: int(x, 0), default=8996, help="frame bytes without FCS (default 8996: 9000 on the wire)")
        s.add_argument("--len-max", type=lambda x: int(x, 0), default=0)
        s.add_argument("--mask", type=lambda x: int(x, 0), default=0, help="random length: len + (random & mask), at most len-max")
        s.add_argument("--gap", type=int, default=0, help="idle cycles between frames")
        s.add_argument("--count", type=lambda x: int(x, 0), default=0)
        s.add_argument("--seed", type=lambda x: int(x, 0), default=0x9E3779B97F4A7C15)
        s.add_argument("--tries", type=int, default=6)
        s.add_argument("--wait", type=float, default=3.0)
        s.add_argument("--recover", choices=["gt", "serdes", "mixed"], default="gt",
                       help="bring-up tries: gt (default: GT TX datapath of the sender + RX resets), "
                            "serdes (DCMAC TX serdes of the sender + RX resets), mixed (serdes "
                            "alternating with the DCMAC reset sequence)")

    sub.add_parser("id").set_defaults(fn=cmd_id)
    s = sub.add_parser("phy")
    s.add_argument("--port", type=int, choices=sorted(PORTS))
    s.add_argument("--loopback", type=int, choices=sorted(LOOPBACK))
    s.add_argument("--wait", type=float, default=1.0)
    s.set_defaults(fn=cmd_phy)
    s = sub.add_parser("restart")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--dcmac", type=int, choices=(0, 1))
    g.add_argument("--port", type=int, choices=sorted(PORTS))
    s.add_argument("--serdes", action="store_true",
                   help="with --port: only the DCMAC TX serdes reset of the port, GT untouched")
    s.add_argument("--wait", type=float, default=3.0)
    s.set_defaults(fn=cmd_restart)
    s = sub.add_parser("speed", help="100G / 200G per DCMAC (QSFP_DYNAMIC builds)")
    s.add_argument("rate", type=int, choices=sorted(RATE))
    s.add_argument("--dcmac", type=int, choices=(0, 1), help="default both")
    s.add_argument("--fec", choices=sorted(FEC_MODE), help="default rs528 at 100G, rs544-200g at 200G")
    s.add_argument("--wait", type=float, default=1.0)
    s.set_defaults(fn=cmd_speed)
    s = sub.add_parser("up")
    paths(s)
    s.set_defaults(fn=cmd_up)
    sub.add_parser("dcmac").set_defaults(fn=cmd_dcmac)
    sub.add_parser("fault").set_defaults(fn=cmd_fault)
    sub.add_parser("stop", help="generators off").set_defaults(fn=lambda bar, a: 0 if stop(all_ports(bar)) else 1)
    s = sub.add_parser("run")
    paths(s)
    s.add_argument("--seconds", type=float, default=600)
    s.add_argument("--interval", type=float, default=5.0)
    s.add_argument("--no-up", action="store_true", help="do not touch the links first")
    s.add_argument("--json", help="append the results to this file")
    s.set_defaults(fn=cmd_run)
    s = sub.add_parser("lengths")
    paths(s)
    s.add_argument("--seconds", type=float, default=10)
    s.add_argument("--interval", type=float, default=2.0)
    s.set_defaults(fn=cmd_lengths)
    args = ap.parse_args()
    return args.fn(Bar(args.device), args)


if __name__ == "__main__":
    sys.exit(main())
