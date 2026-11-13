#!/usr/bin/env python3
"""QDMA streaming test of the nictest RM: qnoc_lb at PF2 BAR2 0x0600_0000.

qnoc_lb sits behind qdma_noc_rm in the service layer; the CPM5's QDMA
streams reach it over the NoC through the NIC static (nic/shell/).
It consumes H2C packets (SINK, checking the pattern), returns them as C2H
packets with completions (LOOPBACK), or generates C2H packets (GEN). The
pattern: the 32-bit word at byte offset o of a packet is o ^ SEED.

Host side: one ST queue pair through the SLASH driver (/dev/slash_qdma_ctl0,
PF1), buffers from SLASH_QDMA_IOCTL_BUF_CREATE, transfers with
SLASH_QDMA_QPAIR_IOCTL_TRANSFER. libqdma cuts an H2C transfer into packets
of dev_addr bytes (at most the 4096 B page), which is --pkt here.

  sudo ./qnoc_lb.py -d 0000:1b:00 status
  sudo ./qnoc_lb.py -d 0000:1b:00 h2c  --bytes 64M --pkt 4096     # SINK + check
  sudo ./qnoc_lb.py -d 0000:1b:00 loop --bytes 1M  --pkt 4096     # LOOPBACK, data compared
  sudo ./qnoc_lb.py -d 0000:1b:00 gen  --count 256 --pkt 4096     # GEN -> C2H
  sudo ./qnoc_lb.py -d 0000:1b:00 gen  --req-first                # C2H request before the packets
  sudo ./qnoc_lb.py -d 0000:1b:00 regs                            # QDMA registers (read only)
  sudo ./qnoc_lb.py -d 0000:1b:00 loop --skip-qid 3               # use a queue id not used yet

C2H needs the SLASH driver of this branch (driver/slash_qdma.c: ST C2H
queues with a completion trigger, prefetch and fetch credits). Without the
trigger C2H transfers time out (10 s); without fetch credits the hardware
fetches descriptors ahead of the packets and a later queue's packets land in
freed buffers. h2c does not need completions.

loop runs H2C and C2H as two one-part transfers on two threads, not one
two-part transfer: the driver waits for the first part of a two-part transfer
with wait_for_completion (no timeout, not killable), so an H2C that stalls
would hang the process until a reboot. Each one-part transfer times out.
"""
import argparse
import ctypes
import fcntl
import mmap
import os
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bar import Bar  # noqa: E402  (single 32-bit BAR loads and stores)

LB = 0x0600_0000
LB_HZ = 250e6                  # qnoc_lb clock (service_clk)
CTL = "/dev/slash_qdma_ctl0"
ID = 0x514E_4C42
R = dict(ID=0x000, VERSION=0x004, CAPS=0x008, CTRL=0x010, STATUS=0x014, SEED=0x018, QID_MAP=0x01C,
         GEN_QID=0x020, GEN_LEN=0x024, GEN_COUNT=0x028, CMPT_SIZE=0x02C, PKT_ID=0x030,
         LAST_H2C=0x0E0, LAST_STS=0x0E4)
C64 = dict(CYCLES=0x040, H2C_PKTS=0x048, H2C_BYTES=0x050, H2C_ERR_PKTS=0x058, H2C_BAD_BEATS=0x060,
           H2C_FIRST=0x068, H2C_LAST=0x070, C2H_PKTS=0x078, C2H_BYTES=0x080, C2H_FIRST=0x088,
           C2H_LAST=0x090, CMPT_SENT=0x098, STS_OK=0x0A0, STS_DROP=0x0A8, STS_ERR=0x0B0,
           LONG_PKTS=0x0B8, ZERO_PKTS=0x0C0, DSC_STS=0x0C8, QSTS=0x0D0)
MODES = {"sink": 0, "loop": 1, "gen": 2}
# QDMA CSRs in PF1 BAR0 (read only here). 18-bit event counters, then the rest.
QDMA_STAT = dict(S_AXIS_C2H=0xA88, S_AXIS_WRB=0xA8C, HAS_CMPT=0xBEC, DESC_REQ=0xB18, DESC_RSP_ACC=0xA98,
                 DESC_RSP_CMP=0xA9C, DESC_RSP_PKT=0xA90, DESC_RSP_DROP=0xB10, DESC_RSP_ERR=0xB14,
                 PKG_CMP=0xA94, WRQ_OUT=0xAA0, WPL_REN=0xAA4, WR_CMP=0xB84)
QDMA_ERR = dict(GLBL_ERR_STAT=0x248, FAB_ERR_LOG=0x314, DSC_ERR_STS=0x254, DSC_ERR_LOG0=0x25C,
                DSC_ERR_LOG1=0x260, DSC_ERR_LOG2=0x27C, C2H_ERR_STAT=0xAF0, C2H_FATAL_ERR=0xAF8,
                C2H_FIRST_QID=0xB30)
QDMA_CFG = dict(PFCH_CFG=0xB08, PFCH_CFG_1=0xA80, PFCH_CFG_2=0xA84, PFCH_CACHE_DEPTH=0xBE0, PFCH_CRDT=0xBE8,
                DBG_DMA_0=0xB1C, DBG_DMA_1=0xB20, DBG_DMA_2=0xB24, DBG_DMA_3=0xB28, DBG_DMA_4=0xB88,
                DBG_DMA_5=0xB8C)


# ---------------------------------------------------------------- qnoc_lb registers
class Lb:
    def __init__(self, bar):
        self.bar = bar

    def rd(self, name):
        return self.bar.rd(LB + R[name])

    def wr(self, name, v):
        self.bar.wr(LB + R[name], v)

    def c(self, name):
        lo = self.bar.rd(LB + C64[name])
        hi = self.bar.rd(LB + C64[name] + 4)
        return (hi << 32) | lo

    def check_id(self):
        v = self.rd("ID")
        if v != ID:
            sys.exit(f"no qnoc_lb at BAR2 0x{LB:x} (ID 0x{v:08x}): is the nictest RM loaded?")

    def setup(self, mode, check=True, cmpt=True, seed=0):
        self.wr("SEED", seed)
        ctrl = MODES[mode] | (check << 4) | (cmpt << 5)
        self.wr("CTRL", ctrl | 0x100)          # clear counters
        self.wr("CTRL", ctrl)

    def counters(self):
        return {k: self.c(k) for k in C64}


class Qdma:
    """the QDMA CSRs, read only (PF1 BAR0, one 32-bit load per read)"""

    def __init__(self, bdf):
        if bdf.count(":") == 1:
            bdf = "0000:" + bdf
        self.mv = None
        try:
            fd = os.open(f"/sys/bus/pci/devices/{bdf}.1/resource0", os.O_RDONLY | os.O_SYNC)
            self.mv = memoryview(mmap.mmap(fd, os.fstat(fd).st_size, mmap.MAP_SHARED, mmap.PROT_READ)).cast("I")
        except OSError as e:
            print(f"QDMA registers not readable: {e}")

    def rd(self, regs):
        return {k: int(self.mv[a >> 2]) for k, a in regs.items()} if self.mv is not None else {}

    def snap(self):
        return self.rd(QDMA_STAT)

    def show_delta(self, before):
        """the C2H event counters since snap(), then every error register that is set"""
        now = self.snap()
        if now:
            print("  QDMA C2H: " + ", ".join(f"{k} {(now[k] - before[k]) & 0x3FFFF}" for k in now))
        err = {k: v for k, v in self.rd(QDMA_ERR).items() if v}
        if err:
            print("  QDMA err: " + ", ".join(f"{k} 0x{v:x}" for k, v in err.items()))


def pattern(length, seed):
    """one packet's bytes: the 32-bit word at offset o is o ^ seed"""
    import array
    n = (length + 3) // 4
    words = array.array("I", range(0, 4 * n, 4))
    if sys.byteorder != "little":
        words.byteswap()
    data = bytearray(words.tobytes())
    for k in range(4):
        sb = (seed >> (8 * k)) & 0xFF
        if sb:
            data[k::4] = data[k::4].translate(bytes(b ^ sb for b in range(256)))
    return bytes(data[:length])


def stream(total, pkt, seed):
    """a transfer of total bytes as libqdma packetizes it: packets of pkt bytes, each patterned"""
    full = pattern(pkt, seed)
    out = bytearray(full * (total // pkt))
    if total % pkt:
        out += full[:total % pkt]
    return bytes(out)


# ---------------------------------------------------------------- SLASH QDMA ioctls
def _iowr(nr, size):
    return (3 << 30) | (size << 16) | (ord("v") << 8) | nr


class QpairAdd(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("size", "mode", "dir_mask", "mm_channel", "h2c_ring_sz",
                                               "c2h_ring_sz", "cmpt_ring_sz", "qid", "aperture_size")]


class QpairOp(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("size", "qid", "op")]


class FdReq(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("qid", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("qpair_count", ctypes.c_uint32), ("qpair_ids", ctypes.c_uint32 * 2)]


class BufCreate(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("length", ctypes.c_uint64),
                ("granule", ctypes.c_uint32), ("transfer_hint", ctypes.c_uint32)]


class SubXfer(ctypes.Structure):
    _fields_ = [("qpair_index", ctypes.c_uint32), ("direction", ctypes.c_uint32), ("buf_fd", ctypes.c_int32),
                ("pad0", ctypes.c_uint32), ("buf_offset", ctypes.c_uint64), ("dev_addr", ctypes.c_uint64),
                ("length", ctypes.c_uint64)]


class Xfer(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("count", ctypes.c_uint32), ("xfers", SubXfer * 2)]


IOC_QPAIR_ADD = _iowr(0x51, ctypes.sizeof(QpairAdd))
IOC_Q_OP = _iowr(0x52, ctypes.sizeof(QpairOp))
IOC_GET_FD = _iowr(0x53, ctypes.sizeof(FdReq))
IOC_BUF_CREATE = _iowr(0x54, ctypes.sizeof(BufCreate))
IOC_TRANSFER = _iowr(0x56, ctypes.sizeof(Xfer))
MODE_ST = 1
DIR_H2C, DIR_C2H = 1, 2         # no DIR_CMPT: libqdma has Q_CMPT for MM only, an ST C2H queue owns its ring
XFER_H2C, XFER_C2H = 1, 2
OP_START, OP_STOP, OP_DEL = 0, 1, 2


class StQueue:
    """one ST queue pair: add, start, fd; stop and delete on close"""

    def __init__(self, dir_mask, ring_idx=0, start=True):
        self.ctl = os.open(CTL, os.O_RDWR)
        a = QpairAdd(size=ctypes.sizeof(QpairAdd), mode=MODE_ST, dir_mask=dir_mask, mm_channel=0,
                     h2c_ring_sz=ring_idx, c2h_ring_sz=ring_idx, cmpt_ring_sz=ring_idx)
        fcntl.ioctl(self.ctl, IOC_QPAIR_ADD, a)
        self.qid = a.qid
        self.started = False
        self.fd = None
        if not start:                                          # holds the queue id, nothing else
            return
        try:
            fcntl.ioctl(self.ctl, IOC_Q_OP, QpairOp(size=ctypes.sizeof(QpairOp), qid=self.qid, op=OP_START))
            self.started = True
            r = FdReq(size=ctypes.sizeof(FdReq), qid=self.qid, flags=os.O_CLOEXEC, qpair_count=1)
            r.qpair_ids[0] = self.qid
            self.fd = fcntl.ioctl(self.ctl, IOC_GET_FD, r)     # the ioctl returns the queue fd
        except OSError:
            self.close()
            raise

    def buffer(self, length):
        b = BufCreate(size=ctypes.sizeof(BufCreate), flags=os.O_CLOEXEC, length=length)
        bfd = fcntl.ioctl(self.ctl, IOC_BUF_CREATE, b)       # returns the buffer fd
        mm = mmap.mmap(bfd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        return bfd, mm

    def transfer(self, subs):
        """subs: [(direction, buf_fd, length, dev_addr)] run in one call"""
        x = Xfer(size=ctypes.sizeof(Xfer), count=len(subs))
        for i, (d, bfd, length, dev) in enumerate(subs):
            x.xfers[i] = SubXfer(qpair_index=0, direction=d, buf_fd=bfd, buf_offset=0, dev_addr=dev, length=length)
        fcntl.ioctl(self.fd, IOC_TRANSFER, x)

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        for op in ((OP_STOP,) if self.started else ()) + (OP_DEL,):
            try:
                fcntl.ioctl(self.ctl, IOC_Q_OP, QpairOp(size=ctypes.sizeof(QpairOp), qid=self.qid, op=op))
            except OSError as e:
                print(f"queue {self.qid}: op {op} failed: {e}")
        os.close(self.ctl)


# ---------------------------------------------------------------- commands
def size_arg(s):
    s = s.strip().upper()
    mul = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}.get(s[-1:], 1)
    return int(s[:-1] if s[-1:] in "KMG" else s, 0) * mul


def show_counters(lb, keys=None):
    c = lb.counters()
    for k in keys or C64:
        print(f"  {k:14} {c[k]}")
    return c


def open_queues(dir_mask, skip):
    """the queue to test, and skip idle queues added first so that it gets queue id skip"""
    idle = [StQueue(dir_mask, start=False) for _ in range(skip)]
    return StQueue(dir_mask), idle


def run_xfers(q, subs, start=None):
    """one one-part transfer per entry of subs, each on its own thread, the last
    first (C2H before H2C). start() runs once they are submitted. Returns errors."""
    errs = [None] * len(subs)

    def one(i):
        try:
            q.transfer([subs[i]])
        except OSError as e:
            errs[i] = e

    th = [threading.Thread(target=one, args=(i,), daemon=True) for i in range(len(subs))]
    for t in reversed(th):
        t.start()
        time.sleep(0.02)
    if start:
        start()
    for t in th:
        t.join(30)
    return [e if not t.is_alive() else TimeoutError("still blocked after 30 s") for e, t in zip(errs, th)]


def show_stuck(lb):
    st = lb.rd("STATUS")
    print(f"  STATUS 0x{st:x}: framing_err {st & 1}, evt_overflow {st >> 1 & 1}, gen_busy {st >> 2 & 1},"
          f" lb_empty {st >> 3 & 1}; next pkt id {lb.rd('PKT_ID')}")
    show_counters(lb, ["H2C_PKTS", "H2C_BYTES", "H2C_BAD_BEATS", "H2C_ERR_PKTS", "C2H_PKTS", "C2H_BYTES",
                       "CMPT_SENT", "STS_OK", "STS_DROP", "STS_ERR", "DSC_STS", "QSTS"])


def show_diff(got, exp, pkt, seed):
    """how got differs from exp: per packet, and the first bad words decoded
    (word ^ seed is the offset in the packet the word was generated for)"""
    n = min(len(got), len(exp))
    bad = [i for i in range(0, n, 4) if got[i:i + 4] != exp[i:i + 4]]
    if not bad:
        return
    pkts = sorted({i // pkt for i in bad})
    untouched = sum(1 for i in bad if got[i:i + 4] == b"\xCC" * 4)
    print(f"  {len(bad)} of {n // 4} words differ, {untouched} still 0xCC (never written);"
          f" packets {pkts[:16]}{' ...' if len(pkts) > 16 else ''}")
    print(f"  bad words in the first bad packet: {sum(1 for i in bad if i // pkt == pkts[0])}")
    print("     offset  packet    +off        got   expected  got^seed")
    for i in bad[:12]:
        g = int.from_bytes(got[i:i + 4], "little")
        e = int.from_bytes(exp[i:i + 4], "little")
        print(f"  {i:9} {i // pkt:7} {i % pkt:7}   {g:08x}   {e:08x}  {g ^ seed:08x}")


def card_gbps(nbytes, first, last):
    return nbytes * 8 / ((last - first + 1) / LB_HZ) / 1e9 if last > first else 0.0


def cmd_status(lb, args):
    lb.check_id()
    st = lb.rd("STATUS")
    ctrl = lb.rd("CTRL")
    mode = {v: k for k, v in MODES.items()}.get(ctrl & 3, "?")
    print(f"qnoc_lb version 0x{lb.rd('VERSION'):08x}, buffer {lb.rd('CAPS')} beats; mode {mode},"
          f" check {ctrl >> 4 & 1}, cmpt {ctrl >> 5 & 1}; seed 0x{lb.rd('SEED'):08x}; next pkt id {lb.rd('PKT_ID')}")
    print(f"  status: framing_err {st & 1}, evt_overflow {st >> 1 & 1}, gen_busy {st >> 2 & 1},"
          f" lb_empty {st >> 3 & 1}")
    h = lb.rd("LAST_H2C")
    s = lb.rd("LAST_STS")
    print(f"  last H2C: qid {h & 0xFFF} len {h >> 16}; last C2H status: qid {s & 0xFFF}"
          f" drop {s >> 16 & 1} error {s >> 17 & 1}")
    show_counters(lb)


def cmd_h2c(lb, args):
    lb.check_id()
    seed = args.seed
    lb.setup("sink", check=True, cmpt=False, seed=seed)
    q = StQueue(DIR_H2C)
    print(f"ST queue {q.qid} (H2C); {args.bytes} B in {args.pkt} B packets, {args.reps} transfer(s)")
    try:
        bfd, mm = q.buffer(args.bytes)
        mm[:] = stream(args.bytes, args.pkt, seed)
        t0 = time.time()
        for _ in range(args.reps):
            q.transfer([(XFER_H2C, bfd, args.bytes, args.pkt)])
        dt = time.time() - t0
        mm.close()
        os.close(bfd)
    finally:
        q.close()
    time.sleep(0.01)
    c = show_counters(lb, ["H2C_PKTS", "H2C_BYTES", "H2C_BAD_BEATS", "H2C_ERR_PKTS", "ZERO_PKTS", "LONG_PKTS"])
    exp_pkts = args.reps * -(-args.bytes // args.pkt)
    total = args.reps * args.bytes
    ok = c["H2C_BYTES"] == total and c["H2C_PKTS"] == exp_pkts and c["H2C_BAD_BEATS"] == 0 and c["H2C_ERR_PKTS"] == 0
    print(f"  host {total * 8 / dt / 1e9:.2f} Gb/s ({dt * 1e3:.1f} ms); card {card_gbps(total, c['H2C_FIRST'], c['H2C_LAST']):.2f}"
          f" Gb/s first to last beat; queue qid seen by the card: {lb.rd('LAST_H2C') & 0xFFF}")
    print("h2c: PASS" if ok else f"h2c: FAIL (expected {exp_pkts} packets, {total} B, no bad beats)")


def cmd_loop(lb, args):
    lb.check_id()
    seed = args.seed
    if args.pkt > 4096:
        sys.exit("--pkt must be at most 4096 (one C2H buffer)")
    lb.setup("loop", check=True, cmpt=True, seed=seed)
    snap = lb.qdma.snap()
    q, idle = open_queues(DIR_H2C | DIR_C2H, args.skip_qid)
    print(f"ST queue {q.qid} (H2C+C2H); {args.bytes} B in {args.pkt} B packets, looped back")
    try:
        hfd, hm = q.buffer(args.bytes)
        cfd, cm = q.buffer(args.bytes)
        exp = stream(args.bytes, args.pkt, seed)
        hm[:] = exp
        cm[:] = b"\xCC" * args.bytes
        t0 = time.time()
        errs = run_xfers(q, [(XFER_H2C, hfd, args.bytes, args.pkt), (XFER_C2H, cfd, args.bytes, 0)])
        dt = time.time() - t0
        got = bytes(cm[:])
        err = errs[0] or errs[1]
        for m, f in ((hm, hfd), (cm, cfd)):
            m.close()
            os.close(f)
    finally:
        for x in [q] + idle:
            x.close()
    show_stuck(lb)
    lb.qdma.show_delta(snap)
    if err:
        print(f"loop: FAIL after {dt:.1f} s: H2C {errs[0] or 'ok'}; C2H {errs[1] or 'ok'}")
        return
    show_diff(got, exp, args.pkt, seed)
    c = lb.counters()      # not the host's dt: run_xfers sleeps 40 ms around the transfers
    print(f"  card: H2C {card_gbps(args.bytes, c['H2C_FIRST'], c['H2C_LAST']):.2f} Gb/s, "
          f"C2H {card_gbps(args.bytes, c['C2H_FIRST'], c['C2H_LAST']):.2f} Gb/s, first to last beat")
    print("loop: PASS" if got == exp else "loop: FAIL (data differs)")


def cmd_gen(lb, args):
    lb.check_id()
    seed = args.seed
    total = args.count * args.pkt
    snap = lb.qdma.snap()
    q, idle = open_queues(DIR_C2H, args.skip_qid)
    print(f"ST queue {q.qid} (C2H); {args.count} packets of {args.pkt} B generated on qid {q.qid}")
    try:
        lb.setup("gen", check=False, cmpt=True, seed=seed)
        lb.wr("GEN_QID", q.qid)
        lb.wr("GEN_LEN", args.pkt)
        cfd, cm = q.buffer(-(-total // 4096) * 4096)
        cm[:] = b"\xCC" * len(cm)
        start = lambda: lb.wr("GEN_COUNT", args.count)   # starts the generator
        t0 = time.time()
        if args.req_first:
            err = run_xfers(q, [(XFER_C2H, cfd, total, 0)], start)[0]
        else:
            start()
            err = run_xfers(q, [(XFER_C2H, cfd, total, 0)])[0]
        dt = time.time() - t0
        got = bytes(cm[:total])
        cm.close()
        os.close(cfd)
    finally:
        lb.wr("CTRL", 0x20)                    # SINK: stops the generator
        for x in [q] + idle:
            x.close()
    show_stuck(lb)
    lb.qdma.show_delta(snap)
    c = lb.counters()
    if err:
        print(f"gen: FAIL after {dt:.1f} s: {err}")
        return
    exp = stream(total, args.pkt, seed)
    show_diff(got, exp, args.pkt, seed)
    print(f"  card {card_gbps(total, c['C2H_FIRST'], c['C2H_LAST']):.2f} Gb/s, first to last beat")
    print("gen: PASS" if got == exp else "gen: FAIL (data differs)")


def cmd_regs(lb, args):
    for group in (QDMA_STAT, QDMA_ERR, QDMA_CFG):
        for k, v in lb.qdma.rd(group).items():
            print(f"  {k:18} 0x{v:08x}")


# srcversions of slash modules known to carry the ST C2H fix (completion
# trigger, prefetch, fetch credits). The stock DKMS module (46DAD330...) does
# not: on it C2H times out and leaves fetched descriptors in the CPM, so a
# later queue's packets land in freed host pages (no IOMMU on the test host).
# srcversions of slash builds that carry the ST C2H fix (cd90b07):
#   77FD64BB...  slash_st_fix.ko (2026-11-06)
#   086CFCC7...  slash_rpuq.ko, external-producer queues (2026-11-10)
#   7E25FA77...  slash_rpuq.ko, plus EXT_FILL_MM (2026-11-11)
ST_FIXED_SRCVERSIONS = {"77FD64BB49F8D1397BDCF25", "086CFCC7255A07708F853DA",
                        "7E25FA7788B2723EBD3114D"}


def check_st_driver(force):
    """refuse ST queues on a slash module that is not known to have the fix"""
    try:
        with open("/sys/module/slash/srcversion") as f:
            v = f.read().strip()
    except OSError:
        sys.exit("slash module not loaded")
    if v in ST_FIXED_SRCVERSIONS:
        return
    msg = (f"slash srcversion {v} is not a known ST-fixed build ({', '.join(sorted(ST_FIXED_SRCVERSIONS))}).\n"
           "The stock driver corrupts host memory on ST C2H. Load the fixed module "
           "(insmod .../slash_st_fix.ko) and reload the static before testing again.")
    if not force:
        sys.exit(msg)
    print(f"warning: {msg}\n--force-driver given, continuing")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="BDF of the V80, e.g. 0000:1b:00")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("regs")
    for name in ("h2c", "loop", "gen"):
        s = sub.add_parser(name)
        s.add_argument("--pkt", type=size_arg, default=4096, help="packet bytes (H2C: dev_addr; at most 4096)")
        s.add_argument("--seed", type=lambda x: int(x, 0), default=0x5EED_0000 | (os.getpid() & 0xFFFF))
        if name == "gen":
            s.add_argument("--count", type=int, default=256)
            s.add_argument("--req-first", action="store_true",
                           help="submit the C2H request before starting the generator")
        else:
            s.add_argument("--bytes", type=size_arg, default=1 << 20)
        if name != "h2c":
            s.add_argument("--skip-qid", type=int, default=0, help="add this many idle queues first")
        if name == "h2c":
            s.add_argument("--reps", type=int, default=1)
        s.add_argument("--force-driver", action="store_true",
                       help="run even if the loaded slash module is not a known ST-fixed build")
    args = ap.parse_args()
    if os.geteuid() != 0:
        sys.exit("run as root (BAR2 mapping, /dev/slash_qdma_ctl0)")
    for a in ("bytes",):
        if hasattr(args, a) and getattr(args, a) % 4096:
            sys.exit("--bytes must be a multiple of 4096 (SLASH buffers are page multiples)")
    if args.cmd in ("h2c", "loop", "gen"):
        check_st_driver(args.force_driver)
    lb = Lb(Bar(args.device))
    lb.qdma = Qdma(args.device)
    {"status": cmd_status, "regs": cmd_regs, "h2c": cmd_h2c, "loop": cmd_loop, "gen": cmd_gen}[args.cmd](lb, args)


if __name__ == "__main__":
    main()
