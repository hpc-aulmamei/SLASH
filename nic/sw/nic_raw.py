#!/usr/bin/env python3
"""Raw Ethernet frames through the NIC RM (rtl/nic_core.sv), no network driver.

nic_core at PF2 BAR2 0x0400_0000 has two ports: port 0 = QSFP0 (DCMAC0,
registers at 0x0200_0000) and port 1 = QSFP2 (DCMAC1, 0x0300_0000), one
200GAUI-4 / 100CAUI-4 port per DCMAC. Lab setup: a cable between QSFP0 and
QSFP2. Frames go host -> H2C queue -> port A -> cable -> port B -> C2H queue
of port B -> host, with SLASH ST queues as qnoc_lb.py opens them.

  sudo ./nic_raw.py -d 0000:1b:00 status
  sudo ./nic_raw.py -d 0000:1b:00 up                       # both links up (resets until stable)
  sudo ./nic_raw.py -d 0000:1b:00 speed 100 --port 0       # QSFP0 to 100G (RS-FEC 528)
  sudo ./nic_raw.py -d 0000:1b:00 speed 200 --port 0
  sudo ./nic_raw.py -d 0000:1b:00 loopback near-pma --port 0   # GT loopback (off to undo)
  sudo ./nic_raw.py -d 0000:1b:00 send --from 0 --len 1500 --frames 1024
  sudo ./nic_raw.py -d 0000:1b:00 send --from 1 --len 60 --frames 4096
  sudo ./nic_raw.py -d 0000:1b:00 send --from 0 --len 1500 --frames 1024 --self   # GT loopback on port 0
  sudo ./nic_raw.py -d 0000:1b:00 dcmac                    # DCMAC counters since the last read
  sudo ./nic_raw.py -d 0000:1b:00 qdma                     # QDMA errors latched (read only)

send: one ST queue pair per port. Port p's TX_QBASE and RX_QID are set to its
queue (TX_QCNT 1), so H2C packets of port A's queue leave on port A, and
frames that arrive on port B go to port B's queue. H2C: one transfer per
frame, each frame on its own page of the H2C buffer (libqdma also cuts a
transfer where the pages of the buffer are not physically contiguous, so a
frame that crossed a page would leave as two packets: seen on hardware
2026-11-15, 1124 packets for 1024 frames). C2H: one transfer of frames * len
bytes on port B; libqdma packs the packets back to back by the completion
length. Frames are <dst = port B's MAC> <src = port A's MAC> <0x88B5>
<32-bit index> <pattern>. --len 60..4096 (the C2H buffer is 4096 B); frames
is rounded up so that frames * len is a multiple of 4096. The DCMAC adds and
strips the FCS.

send refuses to start when the QDMA has a C2H error latched (a fatal C2H
error left the host dead on 2026-11-15 when more transfers followed it):
reload the static and the RM first. It stops at the first failure.

The rate (speed) is per port: each port has its own DCMAC. The ConnectX-5
needs TX_MODE[22] (four-lane PMD) for 100G RS-FEC; speed sets it.
"""
import argparse
import ctypes
import fcntl
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bar import Bar  # noqa: E402
from qnoc_lb import (DIR_C2H, DIR_H2C, IOC_TRANSFER, QDMA_ERR, XFER_C2H, XFER_H2C, Qdma,  # noqa: E402
                     StQueue, SubXfer, Xfer, check_st_driver, pattern)

NIC = 0x0400_0000
NIC_ID = 0x4E49_4331                 # "NIC1"
NIC_HZ = 250e6                       # service_clk
DCMAC = (0x0200_0000, 0x0300_0000)   # port p -> DCMAC p, DCMAC port 0
QSFP = (0, 2)                        # port p -> QSFP cage label
QUAD = ("GTM_QUAD_X1Y7", "GTM_QUAD_X0Y10")
ETYPE = 0x88B5                       # IEEE local experimental

# global registers
G = dict(ID=0x000, VERSION=0x004, SCRATCH=0x008, BUILD=0x00C, CTRL=0x020, PKT_ID=0x024, MAX_LEN=0x028,
         CMPT_SIZE=0x02C, EVT=0x060)
G64 = dict(CYCLES=0x010, H2C_NOPORT=0x030, H2C_ZERO=0x038, STS_OK=0x040, STS_DROP=0x048, STS_ERR=0x050,
           CMPT_SENT=0x058)
# per-port registers at 0x100 + 0x100 * p
P = dict(PCTRL=0x00, RX_QID=0x04, TX_QBASE=0x08, TX_QCNT=0x0C, MAC_LO=0x10, MAC_HI=0x14, PHY_CTRL=0x18,
         PHY_STATUS=0x1C, RX_DROPS=0x20)
P64 = dict(TX_FRAMES=0x40, TX_BYTES=0x48, RX_FRAMES=0x50, RX_BYTES=0x58, RX_HOST=0x60, RX_FILTERED=0x68,
           RX_TOOLONG=0x70, HAIRPIN=0x78, TX_H2C=0x80, TX_DROP=0x88, C2H_DROP=0x90,
           RX_MALFORMED=0x98)          # RX_MALFORMED: nic_core VERSION 0x00010001 and later
PCTRL_BITS = ["rx_en", "tx_en", "promisc", "hairpin", "swap"]

# PHY_CTRL / PHY_STATUS (rtl/dcmac/dcmac_phy_ctl.v)
LOOPBACK = {"off": 0, "near-pcs": 1, "near-pma": 2, "far-pma": 4, "far-pcs": 6}
PHY_BITS = ["gt_tx_reset_done", "gt_rx_reset_done", "gt_powergood", "dcmac_mmcm_locked",
            "dcmac_tx_core_reset", "dcmac_rx_core_reset", "dcmac_rx_serdes_reset", "axis_out_of_reset",
            "sequencer_held", "rate_100g"]
SPEED_BIT = 8

# DCMAC per-channel registers at DCMAC + 0x1000 * (channel + 1); port 0 = channels 0 (and 1 at 200G)
D_FLUSH_RX, D_FLUSH_TX = 0x030, 0x038
D_TX_MODE, D_RX_MODE = 0x040, 0x044
D_CTL_RX, D_CTL_TX = 0x0F0, 0x0F8
D_TICK_RX, D_TICK_TX = 0x0F4, 0x0FC
D_TX_MAC_RT = 0x104
D_RX_STATS, D_TX_STATS = 0x400, 0x200
D_TX_READY, D_RX_PHY, D_RX_PHY_RT, D_RX_READY = 0x808, 0xC00, 0xC04, 0xC08
D_BLOCK_LOCK, D_LANE_SYNC = 0xC0C, 0xC10
D_RX_FEC, D_RX_FEC_RT = 0xC34, 0xC38
D_FEC_CW, D_FEC_CORR, D_FEC_UNCORR = 0xE48, 0xE50, 0xE58
RATE = {100: 0, 200: 1}
FEC_MODE = {"rs528": 0b00101, "rs544": 0b00100, "rs544-200g": 0b01000, "none": 0b00000}
DCMAC_RX = [("total_packets", 0x10), ("good_packets", 0x18), ("total_bytes", 0x00), ("small", 0x20),
            ("bad_code", 0x28), ("bad_fcs", 0x30), ("packet_bad_fcs", 0x38), ("stomped_fcs", 0x40),
            ("truncated", 0x48), ("toolong", 0xB0), ("jabber", 0xC0), ("oversize", 0xC8)]
DCMAC_TX = [("total_packets", 0x10), ("good_packets", 0x18), ("total_bytes", 0x00),
            ("frame_error", 0x20), ("bad_fcs", 0x28)]


def rd64(rd, off):
    """HI, LO, HI until the two HI reads agree"""
    for _ in range(8):
        hi = rd(off + 4)
        lo = rd(off)
        if rd(off + 4) == hi:
            break
    return hi << 32 | lo


def mac_str(m):
    return ":".join(f"{m >> (40 - 8 * i) & 0xFF:02x}" for i in range(6))


class Port:
    """one NIC port: its nic_core registers and its DCMAC port 0"""

    def __init__(self, bar, p):
        self.bar, self.p = bar, p
        self.base = NIC + 0x100 + 0x100 * p
        self.d = DCMAC[p]
        self.block = self.d + 0x1000          # DCMAC port 0 (channel 0)

    def rd(self, name):
        return self.bar.rd(self.base + P[name])

    def wr(self, name, v):
        self.bar.wr(self.base + P[name], v)

    def c(self, name):
        return rd64(lambda o: self.bar.rd(self.base + o), P64[name])

    def counters(self):
        return {k: self.c(k) for k in P64}

    def mac(self):
        return (self.rd("MAC_HI") & 0xFFFF) << 32 | self.rd("MAC_LO")

    def mac_rd(self, off):
        return self.bar.rd(self.block + off)

    # ------------------------------------------------ link
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
        ctrl = self.rd("PHY_CTRL") & ~0xF0
        self.wr("PHY_CTRL", ctrl | 1 << bit)
        time.sleep(0.01)
        self.wr("PHY_CTRL", ctrl)

    def loopback(self, mode):
        self.wr("PHY_CTRL", (self.rd("PHY_CTRL") & ~7) | mode)

    def wait_status(self, bit, timeout=2.0):
        """until PHY_STATUS[bit] is 1 (after a reset pulse it first drops)"""
        t_end = time.time() + 0.05
        while time.time() < t_end and self.rd("PHY_STATUS") >> bit & 1:
            time.sleep(0.001)
        t_end = time.time() + timeout
        while time.time() < t_end:
            if self.rd("PHY_STATUS") >> bit & 1:
                return True
            time.sleep(0.005)
        return False

    def rx_reset(self):
        """GT RX datapath, then the DCMAC RX of this port (both channels of a 200G port)"""
        self.pulse(4)
        time.sleep(0.05)
        blocks = [self.block, self.block + 0x1000]
        for b in blocks:
            self.bar.wr(b + D_CTL_RX, 2)
        time.sleep(0.002)
        for b in blocks:
            self.bar.wr(b + D_CTL_RX, 0)

    def tx_serdes_hold(self, on):
        for b in (self.block, self.block + 0x1000):
            v = self.bar.rd(b + D_CTL_TX)
            self.bar.wr(b + D_CTL_TX, v | 2 if on else v & ~2)

    def tx_serdes_reset(self):
        self.tx_serdes_hold(True)
        time.sleep(0.002)
        self.tx_serdes_hold(False)

    def tx_reset(self):
        """GT TX datapath of this quad (PHY_CTRL[6]) in PG369's TX startup order: the
        DCMAC TX serdes of this port held in reset until the GT TX is done again"""
        self.tx_serdes_hold(True)
        self.pulse(6)
        ok = self.wait_status(0)
        time.sleep(0.001)
        self.tx_serdes_hold(False)
        return ok

    def restart(self):
        """the DCMAC/GT reset sequencer again (PHY_CTRL[7]), then the DCMAC TX serdes side"""
        self.pulse(7)
        self.wait_status(0)
        self.wait_status(1)
        t_end = time.time() + 1.0
        while time.time() < t_end and not self.rd("PHY_STATUS") >> 7 & 1:
            time.sleep(0.005)
        self.tx_serdes_reset()

    def tick(self):
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

    def dcmac_counters(self):
        """DCMAC counts since the previous tick; FEC over this port's channels"""
        self.tick()
        rd = self.mac_rd
        out = {k: rd64(rd, D_RX_STATS + o) for k, o in DCMAC_RX}
        out.update({"tx_" + k: rd64(rd, D_TX_STATS + o) for k, o in DCMAC_TX})
        nxt = lambda o: self.bar.rd(self.block + 0x1000 + o)
        for k, o in (("fec_cw", D_FEC_CW), ("fec_corrected", D_FEC_CORR), ("fec_uncorrected", D_FEC_UNCORR)):
            out[k] = rd64(rd, o) + rd64(rd, o + 0x18) + rd64(nxt, o) + rd64(nxt, o + 0x18)
        return out

    def phy_show(self):
        up, rx, fec, tx = self.link()
        ctrl, st = self.rd("PHY_CTRL"), self.rd("PHY_STATUS")
        lb = {v: k for k, v in LOOPBACK.items()}.get(ctrl & 7, ctrl & 7)
        print(f"port {self.p} (QSFP{QSFP[self.p]}, {QUAD[self.p]}, DCMAC{self.p}): link {'up' if up else 'DOWN'}, "
              f"{'100' if st >> 9 & 1 else '200'}G, rx phy {rx:#05x}, rx fec {fec:#04x}, tx mac {tx:#04x}, "
              f"block lock {self.mac_rd(D_BLOCK_LOCK):#x}, lane sync {self.mac_rd(D_LANE_SYNC):#x}")
        print(f"  PHY_CTRL {ctrl:#06x}: loopback {lb}, cdr hold {ctrl >> 3 & 1}")
        print(f"  PHY_STATUS {st:#06x}: " + " ".join(f"{b}={st >> i & 1}" for i, b in enumerate(PHY_BITS)))


class Nic:
    def __init__(self, bar):
        self.bar = bar
        self.ports = [Port(bar, 0), Port(bar, 1)]

    def rd(self, name):
        return self.bar.rd(NIC + G[name])

    def wr(self, name, v):
        self.bar.wr(NIC + G[name], v)

    def c(self, name):
        return rd64(lambda o: self.bar.rd(NIC + o), G64[name])

    def check_id(self):
        v = self.rd("ID")
        if v != NIC_ID:
            sys.exit(f"no nic_core at BAR2 0x{NIC:08x} (ID 0x{v:08x}, expected 0x{NIC_ID:08x}): is the NIC RM loaded?")

    def clear(self):
        self.wr("CTRL", 1)


# ---------------------------------------------------------------- commands
def cmd_status(nic, args):
    nic.check_id()
    print(f"nic_core version 0x{nic.rd('VERSION'):08x} build {nic.rd('BUILD')}; max_len {nic.rd('MAX_LEN')},"
          f" cmpt_size {nic.rd('CMPT_SIZE')}, next pkt id {nic.rd('PKT_ID')}, evt 0x{nic.rd('EVT'):x}"
          " (bit 0 event overflow, bit 1 framing error)")
    print("  " + ", ".join(f"{k} {nic.c(k)}" for k in G64))
    for pt in nic.ports:
        pc = pt.rd("PCTRL")
        flags = " ".join(n for i, n in enumerate(PCTRL_BITS) if pc >> i & 1) or "-"
        d = pt.rd("RX_DROPS")
        print(f"port {pt.p} (QSFP{QSFP[pt.p]}): mac {mac_str(pt.mac())}, {flags}; rx_qid {pt.rd('RX_QID')},"
              f" tx_qbase {pt.rd('TX_QBASE')} x {pt.rd('TX_QCNT')}; rx_path drops: bad fcs {d & 0xFFF},"
              f" overflow {d >> 12 & 0xF}")
        c = pt.counters()
        print("  " + ", ".join(f"{k} {v}" for k, v in c.items()))
        pt.phy_show()
    return 0


def cmd_up(nic, args):
    """Links up. For a port that is down, each try (--recover gt, the default): the GT
    TX datapath reset of its sender (the far end, or itself with --self) with the
    sender's DCMAC TX serdes held, then its own GT RX + DCMAC RX reset. A failing
    200G port is often FEC lane-locked but not aligned (rx fec 0x3c); the DCMAC reset
    sequence (--recover mixed, the old default) does not clear that and puts working
    ports into the same state. As qsfp_loop.py bring_up (slash-qsfp4 3ba52a21),
    measured 2026-11-15 on the same QSFP0-QSFP2 cable: gt up within 3 tries in 20/20."""
    nic.check_id()
    ports = [nic.ports[p] for p in args.port]
    peer = {p.p: (p if args.self else nic.ports[1 - p.p]) for p in ports}
    t0 = time.time()
    for attempt in range(args.tries + 1):
        down = [p for p in ports if not p.stable()]
        if not down:
            print(f"links {', '.join(f'port {p.p}' for p in ports)} up after {time.time() - t0:.2f} s")
            return 0
        if attempt == args.tries:
            break
        if args.recover == "gt":
            senders = sorted({peer[p.p].p for p in down})
            print(f"try {attempt + 1}/{args.tries}: GT TX reset of port(s) {senders}, RX reset of "
                  f"{[p.p for p in down]} " + ", ".join(f"(port {p.p} rx fec {p.link()[2]:#04x})" for p in down),
                  flush=True)
            for n in senders:
                if not nic.ports[n].tx_reset():
                    print(f"port {n}: GT TX reset not done after 2 s", flush=True)
            for p in down:
                p.rx_reset()
        elif args.recover == "serdes" or attempt % 2 == 0:
            print(f"try {attempt + 1}/{args.tries}: TX serdes reset of the senders, RX reset of "
                  f"{[p.p for p in down]}", flush=True)
            for p in down:
                peer[p.p].tx_serdes_reset()
            for p in down:
                p.rx_reset()
        else:
            print(f"try {attempt + 1}/{args.tries}: reset sequence of DCMAC {[p.p for p in down]}", flush=True)
            for p in down:
                p.restart()
            time.sleep(1.5)
            for p in nic.ports:
                p.rx_reset()
        t_end = time.time() + args.wait
        while time.time() < t_end and not all(p.link()[0] for p in ports):
            time.sleep(0.05)
    for p in ports:
        p.phy_show()
    print("up: FAIL")
    return 1


def dcmac_mode(rate, fec):
    """(TX_MODE, RX_MODE): data rate, tick by register, PMA lane mux (1 PAM4 200GAUI-4,
    0 NRZ 100CAUI-4), FEC, and TX_MODE[22] four-lane PMD at 100G RS-FEC (ConnectX-5)"""
    lane_mux = 1 if rate == 200 else 0
    four_lane = int(rate == 100 and fec.startswith("rs"))
    tx = RATE[rate] | 1 << 4 | lane_mux << 9 | FEC_MODE[fec] << 16 | four_lane << 22
    rx = RATE[rate] | 1 << 11 | lane_mux << 12 | FEC_MODE[fec] << 16
    return tx, rx


def set_speed(pt, rate, fec):
    """port pt (its own DCMAC) to rate, in the order of AMD's dynamic example and
    qsfp_loop.py: flush and reset the port's channels, GT line rate and DCMAC
    configuration (PHY_CTRL[8]), wait for the quad, reset the DCMAC, port mode, release"""
    bar, d = pt.bar, pt.d
    chans = [d + 0x1000 * (c + 1) for c in range(6)]
    print(f"port {pt.p}: before: TX_MODE {bar.rd(chans[0] + D_TX_MODE):#010x} RX_MODE {bar.rd(chans[0] + D_RX_MODE):#010x}")
    for b in chans[:2]:
        bar.wr(b + D_FLUSH_RX, 1)
        bar.wr(b + D_FLUSH_TX, 1)
    time.sleep(0.001)
    for b in chans[:2]:
        bar.wr(b + D_CTL_RX, 3)
        bar.wr(b + D_CTL_TX, 3)
    v = pt.rd("PHY_CTRL") & ~(1 << SPEED_BIT)
    pt.wr("PHY_CTRL", v | (rate == 100) << SPEED_BIT)
    if not (pt.wait_status(0) and pt.wait_status(1)):
        print(f"port {pt.p}: GT reset not done after the rate change")
    if (pt.rd("PHY_STATUS") >> 9 & 1) != (rate == 100):
        print(f"port {pt.p}: PHY_STATUS rate bit does not follow PHY_CTRL; built with NIC_DYNAMIC=0?")
    for b in [d] + chans:
        bar.wr(b + D_CTL_RX, 0xFFFF_FFFF)
        bar.wr(b + D_CTL_TX, 0xFFFF_FFFF)
    if rate == 100:                                       # AMD ETHIP-4113: all channels to 100G first
        tx, rx = dcmac_mode(100, fec)
        for b in chans:
            bar.wr(b + D_TX_MODE, tx)
            bar.wr(b + D_RX_MODE, rx)
    tx, rx = dcmac_mode(rate, fec)
    bar.wr(chans[0] + D_TX_MODE, tx)
    bar.wr(chans[0] + D_RX_MODE, rx)
    for b in [d] + chans:
        bar.wr(b + D_CTL_RX, 0)
        bar.wr(b + D_CTL_TX, 0)
    time.sleep(0.001)
    for b in chans[:2]:
        bar.wr(b + D_FLUSH_RX, 0)
        bar.wr(b + D_FLUSH_TX, 0)
    print(f"port {pt.p}: {rate}G {fec}: TX_MODE {bar.rd(chans[0] + D_TX_MODE):#010x} "
          f"RX_MODE {bar.rd(chans[0] + D_RX_MODE):#010x}")


def cmd_speed(nic, args):
    nic.check_id()
    fec = args.fec or ("rs528" if args.rate == 100 else "rs544-200g")
    for p in args.port:
        set_speed(nic.ports[p], args.rate, fec)
    time.sleep(args.wait)
    for p in args.port:
        nic.ports[p].phy_show()
    print("then: nic_raw.py up (both ends of the cable must run the same rate)")
    return 0


def cmd_loopback(nic, args):
    nic.check_id()
    for p in args.port:
        nic.ports[p].loopback(LOOPBACK[args.mode])
    time.sleep(0.1)
    for p in args.port:
        nic.ports[p].rx_reset()
    time.sleep(0.2)
    for p in args.port:
        nic.ports[p].phy_show()
    return 0


def cmd_qdma(nic, args):
    """the QDMA error registers, read only (opens no queue): safe while the netdevs run"""
    qdma = Qdma(args.device)
    err = {k: v for k, v in qdma.rd(QDMA_ERR).items() if v}
    print("QDMA err: " + (", ".join(f"{k} 0x{v:x}" for k, v in err.items()) or "none"))
    bad = qdma_c2h_errors(qdma)
    print("QDMA C2H errors latched: " + ("; ".join(bad) if bad else "none"))
    return 1 if bad else 0


def cmd_dcmac(nic, args):
    nic.check_id()
    for pt in nic.ports:
        c = pt.dcmac_counters()
        print(f"port {pt.p} (QSFP{QSFP[pt.p]}) since the last read: " + ", ".join(f"{k} {v}" for k, v in c.items()))
    return 0


def frames(n, length, dst, src, seed):
    """n frames of length bytes, back to back"""
    hdr = dst.to_bytes(6, "big") + src.to_bytes(6, "big") + ETYPE.to_bytes(2, "big")
    body = pattern(length - 18, seed)
    return b"".join(hdr + struct.pack("<I", i) + body for i in range(n))


def show_frame_diff(got, exp, length, n):
    bad = [i for i in range(n) if got[i * length:(i + 1) * length] != exp[i * length:(i + 1) * length]]
    if not bad:
        return 0
    unwritten = sum(1 for i in bad if got[i * length:(i + 1) * length] == b"\xCC" * length)
    print(f"  {len(bad)} of {n} frames differ ({unwritten} never written); first {bad[:12]}")
    i = bad[0]
    g, e = got[i * length:(i + 1) * length], exp[i * length:(i + 1) * length]
    off = next(k for k in range(length) if g[k] != e[k])
    idx = struct.unpack_from("<I", g, 14)[0] if length >= 18 else -1
    print(f"  frame {i}: first difference at byte {off}; index field {idx}; got {g[off:off + 16].hex()}"
          f" expected {e[off:off + 16].hex()}")
    return len(bad)


QDMA_ERR_OK = {"GLBL_ERR_STAT": 0x20000}  # ERR_FAB fires on every H2C queue (known, harmless)


def qdma_c2h_errors(qdma):
    """C2H errors the QDMA has latched; empty if none (or if the registers are not readable)"""
    e = qdma.rd(QDMA_ERR)
    bad = []
    if e.get("GLBL_ERR_STAT", 0) & ~QDMA_ERR_OK["GLBL_ERR_STAT"]:
        bad.append(f"GLBL_ERR_STAT 0x{e['GLBL_ERR_STAT']:x}")
    for k in ("C2H_ERR_STAT", "C2H_FATAL_ERR"):
        if e.get(k, 0):
            bad.append(f"{k} 0x{e[k]:x}")
    if e.get("C2H_FIRST_QID", 0) >> 16 & 0x1F:
        bad.append(f"C2H_FIRST_QID 0x{e['C2H_FIRST_QID']:x} (error type {e['C2H_FIRST_QID'] >> 16 & 0x1F},"
                   f" qid {e['C2H_FIRST_QID'] & 0x1FFF})")
    return bad


def h2c_frame(q, bfd, off, length):
    """one H2C packet: length bytes from page offset off of the buffer"""
    x = Xfer(size=ctypes.sizeof(Xfer), count=1)
    x.xfers[0] = SubXfer(qpair_index=0, direction=XFER_H2C, buf_fd=bfd, buf_offset=off, dev_addr=length,
                         length=length)
    fcntl.ioctl(q.fd, IOC_TRANSFER, x)


def cmd_send(nic, args):
    nic.check_id()
    a = nic.ports[args.src]
    b = a if args.self else nic.ports[1 - args.src]
    if not 60 <= args.len <= 4096:
        sys.exit("--len must be 60..4096")
    m = 4096 // math.gcd(args.len, 4096)                 # frames per whole number of pages
    n = -(-args.frames // m) * m
    total = n * args.len
    qdma = Qdma(args.device)
    bad = qdma_c2h_errors(qdma)
    if bad and not args.ignore_qdma_errors:
        print("send: REFUSED, the QDMA has C2H errors latched: " + "; ".join(bad))
        print("  reload the static and the NIC RM before the next send")
        return 2
    for pt in {a.p: a, b.p: b}.values():
        up, rx, fec, tx = pt.link()
        if not up:
            print(f"send: REFUSED, port {pt.p} link down (rx phy {rx:#05x}, tx mac {tx:#04x}); `nic_raw.py up` first")
            return 2
    snap = qdma.snap()
    qa = StQueue(DIR_H2C | DIR_C2H)
    qb = qa if b is a else StQueue(DIR_H2C | DIR_C2H)
    stuck = False
    try:
        # port a sends what qa's H2C carries; port b delivers to qb's C2H
        for pt, q in ((a, qa), (b, qb)):
            pt.wr("TX_QBASE", q.qid)
            pt.wr("TX_QCNT", 1)
            pt.wr("RX_QID", q.qid)
            pt.wr("PCTRL", 0b00011)                      # rx_en, tx_en; no promisc, no hairpin
        dst = b.mac()
        exp = frames(n, args.len, dst, a.mac(), args.seed)
        nic.clear()
        for pt in nic.ports:                             # DCMAC counters from here on
            pt.tick()
        print(f"port {a.p} (queue {qa.qid}) -> {'GT loopback' if b is a else 'cable'} -> port {b.p} (queue {qb.qid}):"
              f" {n} frames of {args.len} B ({total} B), dst {mac_str(dst)}")
        hfd, hm = qa.buffer(n * 4096)                    # frame i at page i
        cbuf = -(-total // 4096) * 4096
        cfd, cm = qb.buffer(cbuf)
        for i in range(n):
            hm[i * 4096:i * 4096 + args.len] = exp[i * args.len:(i + 1) * args.len]
        cm[:] = b"\xCC" * cbuf
        import threading
        c_err = [None]

        def c2h():
            try:
                qb.transfer([(XFER_C2H, cfd, total, 0)])
            except OSError as e:
                c_err[0] = e
        tc = threading.Thread(target=c2h, daemon=True)
        t0 = time.time()
        tc.start()
        time.sleep(0.02)
        h_err, sent = None, 0
        for i in range(n):
            try:
                h2c_frame(qa, hfd, i * 4096, args.len)
            except OSError as e:
                h_err = e
                break
            sent += 1
        tc.join(30)
        dt = time.time() - t0
        if tc.is_alive():
            stuck = True
            c_err[0] = TimeoutError("C2H transfer still blocked after 30 s")
        got = bytes(cm[:total])
        if not stuck:
            for mm, fd in ((hm, hfd), (cm, cfd)):
                mm.close()
                os.close(fd)
    finally:
        if not stuck:
            for q in {id(qa): qa, id(qb): qb}.values():
                q.close()
    ca, cb = a.counters(), b.counters()
    print(f"  port {a.p}: TX_H2C {ca['TX_H2C']}, TX_FRAMES {ca['TX_FRAMES']}, TX_BYTES {ca['TX_BYTES']}, "
          f"TX_DROP {ca['TX_DROP']}")
    print(f"  port {b.p}: RX_FRAMES {cb['RX_FRAMES']}, RX_BYTES {cb['RX_BYTES']}, RX_HOST {cb['RX_HOST']}, "
          f"RX_FILTERED {cb['RX_FILTERED']}, RX_TOOLONG {cb['RX_TOOLONG']}, RX_MALFORMED {cb['RX_MALFORMED']}, "
          f"C2H_DROP {cb['C2H_DROP']}, rx_path drops 0x{b.rd('RX_DROPS'):04x}")
    print(f"  H2C_NOPORT {nic.c('H2C_NOPORT')}, H2C_ZERO {nic.c('H2C_ZERO')}, STS_OK {nic.c('STS_OK')}, "
          f"STS_DROP {nic.c('STS_DROP')}, STS_ERR {nic.c('STS_ERR')}, CMPT_SENT {nic.c('CMPT_SENT')}")
    for pt in {a.p: a, b.p: b}.values():
        c = pt.dcmac_counters()
        print(f"  DCMAC{pt.p}: tx {c['tx_total_packets']} pkts {c['tx_total_bytes']} B, rx {c['total_packets']} pkts"
              f" (good {c['good_packets']}, small {c['small']}, bad fcs {c['bad_fcs']}), fec corrected"
              f" {c['fec_corrected']} uncorrected {c['fec_uncorrected']}")
    qdma.show_delta(snap)
    bad = qdma_c2h_errors(qdma)
    if bad:
        print("  QDMA C2H errors latched: " + "; ".join(bad))
    if stuck:
        print("send: FAIL, C2H transfer stuck; queues left open. Do not run anything else on the card:"
              " reload the static and the NIC RM.")
        sys.stdout.flush()
        os._exit(3)
    if h_err or c_err[0] or bad:
        print(f"send: FAIL after {dt:.1f} s: H2C {h_err or 'ok'} ({sent}/{n} frames submitted);"
              f" C2H {c_err[0] or 'ok'}")
        print("  reload the static and the NIC RM before the next send")
        return 1
    nbad = show_frame_diff(got, exp, args.len, n)
    print(f"  host: {total * 8 / dt / 1e9:.2f} Gb/s over {dt * 1e3:.1f} ms (one H2C transfer per frame)")
    ok = nbad == 0 and cb["RX_HOST"] == n and ca["TX_H2C"] == n
    print("send: PASS" if ok else f"send: FAIL ({nbad} frames differ; expected {n} frames each way)")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="BDF of the V80, e.g. 0000:1b:00")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    s = sub.add_parser("up")
    s.add_argument("--port", type=int, nargs="+", choices=(0, 1), default=[0, 1])
    s.add_argument("--self", action="store_true", help="each port is its own peer (GT loopback)")
    s.add_argument("--tries", type=int, default=10)
    s.add_argument("--wait", type=float, default=2.0)
    s.add_argument("--recover", choices=["gt", "serdes", "mixed"], default="gt",
                   help="gt: GT TX reset of the sender + RX reset (default); mixed: the old one")
    s = sub.add_parser("speed")
    s.add_argument("rate", type=int, choices=(100, 200))
    s.add_argument("--port", type=int, nargs="+", choices=(0, 1), required=True)
    s.add_argument("--fec", choices=sorted(FEC_MODE), help="default rs528 at 100G, rs544-200g at 200G")
    s.add_argument("--wait", type=float, default=0.5)
    s = sub.add_parser("loopback")
    s.add_argument("mode", choices=sorted(LOOPBACK))
    s.add_argument("--port", type=int, nargs="+", choices=(0, 1), required=True)
    sub.add_parser("dcmac")
    sub.add_parser("qdma")
    s = sub.add_parser("send")
    s.add_argument("--from", dest="src", type=int, choices=(0, 1), default=0)
    s.add_argument("--self", action="store_true", help="receive on the sending port (GT loopback set)")
    s.add_argument("--len", type=int, default=1500, help="frame bytes without FCS, 60..4096")
    s.add_argument("--frames", type=int, default=1024)
    s.add_argument("--seed", type=lambda x: int(x, 0), default=0x5EED_0000 | (os.getpid() & 0xFFFF))
    s.add_argument("--force-driver", action="store_true",
                   help="run even if the loaded slash module is not a known ST-fixed build")
    s.add_argument("--ignore-qdma-errors", action="store_true",
                   help="start even if the QDMA has C2H errors latched")
    args = ap.parse_args()
    if os.geteuid() != 0:
        sys.exit("run as root (BAR2 mapping, /dev/slash_qdma_ctl0)")
    if args.cmd == "send":
        # the slash driver's netdevs own nic_core's queues, MACs and PCTRL while attached;
        # up / speed / loopback only touch PHY_CTRL and the DCMAC (link bring-up stays here)
        pf2 = args.device if args.device.count(":") == 2 else "0000:" + args.device
        try:
            with open(f"/sys/bus/pci/devices/{pf2}.2/nic") as f:
                state = f.read().strip()
        except OSError:
            state = ""
        if state.startswith("attached"):
            sys.exit(f"the NIC is attached to the slash driver ({state});\n"
                     f"detach first: echo 0 | sudo tee /sys/bus/pci/devices/{pf2}.2/nic")
    if args.cmd == "send":
        check_st_driver(args.force_driver)
    nic = Nic(Bar(args.device))
    fn = {"status": cmd_status, "up": cmd_up, "speed": cmd_speed, "loopback": cmd_loopback, "dcmac": cmd_dcmac,
          "qdma": cmd_qdma, "send": cmd_send}[args.cmd]
    return fn(nic, args)


if __name__ == "__main__":
    sys.exit(main())
