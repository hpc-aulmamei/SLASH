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

"""External-producer ST H2C queue (SLASH_QDMA_QPAIR_F_EXT_PRODUCER) against
the nictest RM's qnoc_lb in SINK mode.

The driver adds and starts one ST H2C queue, fills its whole ring once from a
kernel buffer (descriptor i = packet i, pattern word(o) = o ^ seed, the same
for every packet), and never posts. Someone else rings the H2C PIDX doorbell:

  --producer host   this tool, through PF1 BAR0 (0x18004 + 0x10 * qidx); it
                    tests the driver mode on any static
  --producer rpu    the R5_1 test ELF (nic/rpu/rpuq.elf, static patched by
                    patch_rpuq.py), through the CPM AXI slave: this tool writes the
                    job into the ELF's mailbox (R5 0x3010_0000 = PF2 BAR4 +
                    0x0410_0000), waits for its ack and drains to the PIDX the
                    ELF reports

Then: EXT_DRAIN (hardware CIDX = final PIDX), STOP, DEL, and the qnoc_lb SINK
counters and pattern check.

  sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer host --packets 10000 --pkt 4096
  sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer rpu --packets 2047        # one doorbell, no CIDX
  sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer rpu --flow --packets 100000 # CIDX via BDF entry
  sudo ./rpuq_ext.py -d 0000:1b:00 rpu-probe                    # QDMA ident through the R5 path
  sudo ./rpuq_ext.py -d 0000:1b:00 rpu-probe --addr 0x98010000  # any R5 address the ELF allows
  sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer host --info-only   # layout, no traffic

PIDX is a 16-bit index modulo the ring size (rngsz); a producer must keep
fewer than rngsz descriptors outstanding: it may advance PIDX only up to
CIDX - 1 (mod rngsz), CIDX from the status writeback (host memory).
"""
import argparse
import ctypes
import fcntl
import json
import mmap
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qnoc_lb as q  # noqa: E402
from bar import Bar  # noqa: E402

F_EXT_PRODUCER = 0x1
DB_H2C_PIDX = 0x18004           # + 0x10 * qidx, in the PF's DMA BAR (PF1 BAR0)
DB_STRIDE = 0x10


class QpairAddX(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("size", "mode", "dir_mask", "mm_channel", "h2c_ring_sz",
                                               "c2h_ring_sz", "cmpt_ring_sz", "qid", "aperture_size",
                                               "flags")]


class ExtInfo(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("size", "qid", "dir", "qidx", "qidx_hw", "func_id")] + \
               [("qbase", ctypes.c_int32)] + \
               [(n, ctypes.c_uint32) for n in ("rngsz", "desc_sz", "c2h_bufsz", "rngsz_cmpt", "cmpt_desc_sz")] + \
               [(n, ctypes.c_uint64) for n in ("desc_bus", "wb_bus", "cmpt_bus", "cmpt_wb_bus")] + \
               [(n, ctypes.c_uint32) for n in ("hw_pidx", "hw_cidx")]


class ExtFill(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("qid", ctypes.c_uint32), ("dir", ctypes.c_uint32),
                ("buf_fd", ctypes.c_int32), ("offset", ctypes.c_uint64), ("stride", ctypes.c_uint32),
                ("pkt_len", ctypes.c_uint32), ("filled", ctypes.c_uint32)]


class ExtDrain(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("size", "qid", "dir", "pidx", "timeout_ms", "hw_cidx")]


IOC_QPAIR_ADD_X = q._iowr(0x51, ctypes.sizeof(q.QpairAdd))   # ioctl number keeps the original size
IOC_EXT_INFO = q._iowr(0x57, ctypes.sizeof(ExtInfo))
IOC_EXT_FILL = q._iowr(0x58, ctypes.sizeof(ExtFill))
IOC_EXT_DRAIN = q._iowr(0x59, ctypes.sizeof(ExtDrain))


class ExtFillMm(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("qid", ctypes.c_uint32), ("dir", ctypes.c_uint32),
                ("buf_fd", ctypes.c_int32), ("offset", ctypes.c_uint64), ("dev_addr", ctypes.c_uint64),
                ("len", ctypes.c_uint32), ("filled", ctypes.c_uint32)]


IOC_EXT_FILL_MM = q._iowr(0x5A, ctypes.sizeof(ExtFillMm))
MODE_MM = 0
MM_CH0, MM_CH1 = 1, 2           # SLASH_QDMA_MM_CHANNEL_0 / _1
DB_C2H_PIDX = 0x18008           # + 0x10 * qidx
HBM_BASE, HBM_SIZE = 0x40_0000_0000, 32 << 30
DDR_BASE, DDR_SIZE = 0x600_0000_0000, 32 << 30


class DmaBar:
    """PF1 BAR0 (QDMA DMA BAR): single 32-bit loads and stores"""

    def __init__(self, bdf):
        if bdf.count(":") == 1:
            bdf = "0000:" + bdf
        fd = os.open(f"/sys/bus/pci/devices/{bdf}.1/resource0", os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(fd, os.fstat(fd).st_size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        self.mv = memoryview(self.mm).cast("I")

    def wr(self, off, v):
        self.mv[off >> 2] = v & 0xFFFFFFFF

    def rd(self, off):
        return int(self.mv[off >> 2])


MBOX_OFF = 0x0410_0000          # PF2 BAR4 offset of R5 0x3010_0000
MBOX_MAGIC = 0x5155_5052
M = dict(magic=0x00, version=0x04, state=0x08, cmd_seq=0x0C, ack_seq=0x10, op=0x14, db_addr=0x18, rngsz=0x1C,
         count=0x20, batch=0x24, flags=0x28, bdf_entry=0x2C, pa_lo=0x30, pa_hi=0x34, bdf_prot=0x38,
         bdf_perm=0x3C, start_pidx=0x40, stall_limit=0x44, err=0x80, posted=0x84, final_pidx=0x88,
         last_cidx=0x8C, doorbells=0x90, polls=0x94, cycles=0x98, heartbeat=0x9C, probe_n=0xA0,
         probe_addr=0xA4, probe_val=0xC4, bdf_rb=0xE4, probe_fmask=0x48, probe_dfsr=0x4C)
OP_PROBE, OP_H2C, OP_BDF = 1, 2, 3
ST_NAMES = {0: "idle", 1: "running", 2: "done", 3: "error"}
ERR_NAMES = {0: "ok", 1: "bad op", 2: "address not allowed", 3: "ring/index", 4: "stall (no CIDX progress)",
             5: "bdf entry", 6: "data abort (address not mapped on this static?); ELF parked, reload it"}
R5_HOST, R5_QDMA = 0x9000_0000, 0x9800_0000
QDMA_IDENT = 0x1FD3_0001        # PF1 BAR0 + 0 on this static


class Mbox:
    """the R5_1 test ELF's mailbox in shared DDR, through PF2 BAR4"""

    def __init__(self, bdf):
        if bdf.count(":") == 1:
            bdf = "0000:" + bdf
        with open(f"/sys/bus/pci/devices/{bdf}.2/config", "rb") as c:
            cmd = int.from_bytes(c.read(6)[4:6], "little")
        if not cmd & 0x2:
            sys.exit(f"{bdf}.2: PCI memory decoding is off (Mem-), BAR4 reads return all-ones;\n"
                     f"  enable it: sudo setpci -s {bdf[5:]}.2 COMMAND=0x02:0x02")
        fd = os.open(f"/sys/bus/pci/devices/{bdf}.2/resource4", os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=MBOX_OFF)
        self.mv = memoryview(self.mm).cast("I")

    def rd(self, k, i=0):
        return int(self.mv[(M[k] >> 2) + i])

    def wr(self, k, v, i=0):
        self.mv[(M[k] >> 2) + i] = v & 0xFFFFFFFF

    def check(self):
        if self.rd("magic") != MBOX_MAGIC:
            sys.exit(f"no RPU test ELF: mailbox magic 0x{self.rd('magic'):08x} (load rpuq.elf on R5 #1)")
        hb = self.rd("heartbeat")
        time.sleep(0.01)
        if self.rd("heartbeat") == hb:
            sys.exit(f"RPU test ELF not running (heartbeat {hb} stuck, state {ST_NAMES.get(self.rd('state'))})")

    def run(self, op, timeout=30.0, **params):
        for k, v in params.items():
            if isinstance(v, list):
                for i, x in enumerate(v):
                    self.wr(k, x, i)
            else:
                self.wr(k, v)
        self.wr("op", op)
        seq = (self.rd("ack_seq") + 1) & 0xFFFFFFFF
        self.wr("cmd_seq", seq)
        t0 = time.time()
        while self.rd("ack_seq") != seq:
            if time.time() - t0 > timeout:
                print(f"  RPU: no ack after {timeout} s: state {ST_NAMES.get(self.rd('state'))},"
                      f" posted {self.rd('posted')}, pidx {self.rd('final_pidx')}")
                return False
            time.sleep(0.001)
        e = self.rd("err")
        if e:
            print(f"  RPU: op {op} error {e} ({ERR_NAMES.get(e, '?')})")
        if e == 6:
            print(f"  RPU: fault address 0x{self.rd('last_cidx'):08x}, DFSR 0x{self.rd('polls'):08x}")
        return e == 0


class ExtQueue:
    def __init__(self, ring_idx):
        self.ctl = os.open(q.CTL, os.O_RDWR)
        a = QpairAddX(size=ctypes.sizeof(QpairAddX), mode=q.MODE_ST, dir_mask=q.DIR_H2C, mm_channel=0,
                      h2c_ring_sz=ring_idx, c2h_ring_sz=ring_idx, cmpt_ring_sz=ring_idx, flags=F_EXT_PRODUCER)
        fcntl.ioctl(self.ctl, IOC_QPAIR_ADD_X, a)
        self.qid = a.qid
        self.started = False
        try:
            self.op(q.OP_START)
            self.started = True
        except OSError:
            self.close()
            raise

    def op(self, op):
        fcntl.ioctl(self.ctl, q.IOC_Q_OP, q.QpairOp(size=ctypes.sizeof(q.QpairOp), qid=self.qid, op=op))

    def info(self):
        i = ExtInfo(size=ctypes.sizeof(ExtInfo), qid=self.qid, dir=q.XFER_H2C)
        fcntl.ioctl(self.ctl, IOC_EXT_INFO, i)
        return i

    def buffer(self, length):
        b = q.BufCreate(size=ctypes.sizeof(q.BufCreate), flags=os.O_CLOEXEC, length=length)
        bfd = fcntl.ioctl(self.ctl, q.IOC_BUF_CREATE, b)
        mm = mmap.mmap(bfd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        return bfd, mm

    def fill(self, bfd, pkt, stride):
        f = ExtFill(size=ctypes.sizeof(ExtFill), qid=self.qid, dir=q.XFER_H2C, buf_fd=bfd, offset=0,
                    stride=stride, pkt_len=pkt)
        fcntl.ioctl(self.ctl, IOC_EXT_FILL, f)
        return f.filled

    def drain(self, pidx, timeout_ms):
        d = ExtDrain(size=ctypes.sizeof(ExtDrain), qid=self.qid, dir=q.XFER_H2C, pidx=pidx,
                     timeout_ms=timeout_ms)
        try:
            fcntl.ioctl(self.ctl, IOC_EXT_DRAIN, d)
            return True, d.hw_cidx
        except OSError as e:
            print(f"  drain: {e}; last cidx {d.hw_cidx}")
            return False, d.hw_cidx

    def close(self):
        for op in ((q.OP_STOP,) if self.started else ()) + (q.OP_DEL,):
            try:
                self.op(op)
            except OSError as e:
                print(f"queue {self.qid}: op {op} failed: {e}")
        os.close(self.ctl)


def host_produce(dbar, info, packets, batch):
    """ring the doorbell as an external producer would: never more than
    rngsz - 1 outstanding, CIDX from EXT_INFO (status writeback)"""
    n = info.rngsz
    db = DB_H2C_PIDX + DB_STRIDE * info.qidx
    posted = 0
    pidx = 0
    t0 = time.time()
    while posted < packets:
        cidx = qe.info().hw_cidx
        free = (cidx - pidx - 1) % n            # descriptors we may still post
        k = min(free, batch, packets - posted)
        if k <= 0:
            if time.time() - t0 > 10:
                print(f"  host producer: no progress (pidx {pidx}, cidx {cidx})")
                break
            continue
        pidx = (pidx + k) % n
        dbar.wr(db, pidx)                       # pidx[15:0], irq_en 0
        posted += k
    return posted, pidx, time.time() - t0


class ExtMmQueue:
    """one direction of an MM external-producer queue pair"""

    def __init__(self, xfer_dir, mm_channel, ring_idx):
        self.dir = xfer_dir
        self.ctl = os.open(q.CTL, os.O_RDWR)
        a = QpairAddX(size=ctypes.sizeof(QpairAddX), mode=MODE_MM,
                      dir_mask=q.DIR_H2C if xfer_dir == q.XFER_H2C else q.DIR_C2H, mm_channel=mm_channel,
                      h2c_ring_sz=ring_idx, c2h_ring_sz=ring_idx, cmpt_ring_sz=ring_idx, flags=F_EXT_PRODUCER)
        fcntl.ioctl(self.ctl, IOC_QPAIR_ADD_X, a)
        self.qid = a.qid
        self.started = False
        try:
            self.op(q.OP_START)
            self.started = True
        except OSError:
            self.close()
            raise

    def op(self, op):
        fcntl.ioctl(self.ctl, q.IOC_Q_OP, q.QpairOp(size=ctypes.sizeof(q.QpairOp), qid=self.qid, op=op))

    def info(self):
        i = ExtInfo(size=ctypes.sizeof(ExtInfo), qid=self.qid, dir=self.dir)
        fcntl.ioctl(self.ctl, IOC_EXT_INFO, i)
        return i

    def buffer(self, length):
        b = q.BufCreate(size=ctypes.sizeof(q.BufCreate), flags=os.O_CLOEXEC, length=length)
        bfd = fcntl.ioctl(self.ctl, q.IOC_BUF_CREATE, b)
        return bfd, mmap.mmap(bfd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

    def fill(self, bfd, dev_addr, length):
        f = ExtFillMm(size=ctypes.sizeof(ExtFillMm), qid=self.qid, dir=self.dir, buf_fd=bfd, offset=0,
                      dev_addr=dev_addr, len=length)
        fcntl.ioctl(self.ctl, IOC_EXT_FILL_MM, f)
        return f.filled

    def drain(self, pidx, timeout_ms):
        d = ExtDrain(size=ctypes.sizeof(ExtDrain), qid=self.qid, dir=self.dir, pidx=pidx, timeout_ms=timeout_ms)
        try:
            fcntl.ioctl(self.ctl, IOC_EXT_DRAIN, d)
            return True, d.hw_cidx
        except OSError as e:
            print(f"  drain: {e}; last cidx {d.hw_cidx}")
            return False, d.hw_cidx

    def close(self):
        for op in ((q.OP_STOP,) if self.started else ()) + (q.OP_DEL,):
            try:
                self.op(op)
            except OSError as e:
                print(f"queue {self.qid}: op {op} failed: {e}")
        os.close(self.ctl)


def cmd_mm(args):
    """host buffer A -> card memory (MM H2C), card memory -> host buffer B (MM C2H); each direction
    started by one producer (the R5 through the CPM AXI slave, or the host through PF1 BAR0);
    descriptor i moves 4 KiB page i of the buffer to/from dev_addr + i * 4 KiB"""
    base, size = (HBM_BASE, HBM_SIZE) if args.mem == "hbm" else (DDR_BASE, DDR_SIZE)
    off = args.offset
    nbytes = args.pages * 4096
    if args.pages < 1 or off % 4096 or off + nbytes > size:
        sys.exit(f"--pages/--offset outside {args.mem} ({size >> 30} GiB)")
    dev = base + off
    # V80 placement (vrtd v80_policy.h): HBM below +16 GiB on channel 0, above on channel 1
    ch = MM_CH1 if args.mem == "hbm" and off >= HBM_SIZE // 2 else MM_CH0
    if args.channel is not None:
        ch = MM_CH0 if args.channel == 0 else MM_CH1
    seed = args.seed if args.seed is not None else int(time.time() * 1000) & 0xFFFFFFFF
    print(f"mm: {args.pages} x 4 KiB = {nbytes / 2**20:.1f} MiB, {args.mem} card address 0x{dev:x}"
          f" (mm channel {ch - 1}), seed 0x{seed:08x}, producer {args.producer}")

    qa = q.Qdma(args.device)
    before = qa.snap()
    qs = []
    bufs = []
    try:
        h2c = ExtMmQueue(q.XFER_H2C, ch, args.ring_idx)
        qs.append(h2c)
        c2h = ExtMmQueue(q.XFER_C2H, ch, args.ring_idx)
        qs.append(c2h)
        ih = h2c.info()
        if args.pages > ih.rngsz - 1:
            sys.exit(f"--pages {args.pages}: at most rngsz - 1 = {ih.rngsz - 1} (one ring, no wrap)")
        blen = ih.rngsz * 4096
        bfd_a, mm_a = h2c.buffer(blen)
        bufs.append((bfd_a, mm_a))
        bfd_b, mm_b = c2h.buffer(blen)
        bufs.append((bfd_b, mm_b))
        pat = q.pattern(nbytes, seed)
        mm_a[:nbytes] = pat
        mm_b[:blen] = b"\xa5" * blen
        h2c.fill(bfd_a, dev, 4096)
        c2h.fill(bfd_b, dev, 4096)
        ih, ic = h2c.info(), c2h.info()
        for name, i in (("H2C", ih), ("C2H", ic)):
            print(f"  {name} queue: qidx {i.qidx} (hw {i.qidx_hw}, func {i.func_id}, qbase {i.qbase}),"
                  f" ring {i.rngsz} x {i.desc_sz} B at 0x{i.desc_bus:x}, wb at 0x{i.wb_bus:x}")

        def produce(i, db_off, label):
            if args.producer == "host":
                t0 = time.time()
                DmaBar(args.device).wr(db_off + DB_STRIDE * i.qidx, args.pages % i.rngsz)
                ok, cidx = (c2h if i is ic else h2c).drain(args.pages, 5000)
                dt = time.time() - t0
                print(f"  {label}: host doorbell PF1 BAR0 + 0x{db_off + DB_STRIDE * i.qidx:x},"
                      f" drain {'ok' if ok else 'TIMEOUT'} (cidx {cidx}), {dt * 1e3:.1f} ms")
                return ok, dt
            db = R5_QDMA + args.fn_stride * i.func_id + db_off + DB_STRIDE * i.qidx
            mbx = Mbox(args.device)
            mbx.check()
            t0 = time.time()
            ok_rpu = mbx.run(OP_H2C, db_addr=db, rngsz=i.rngsz, count=args.pages, batch=args.batch, flags=0,
                             bdf_entry=0, pa_lo=0, pa_hi=0, bdf_prot=0, bdf_perm=0, start_pidx=i.hw_pidx,
                             stall_limit=0)
            ok, cidx = (c2h if i is ic else h2c).drain(mbx.rd("final_pidx"), 5000)
            dt = time.time() - t0
            print(f"  {label}: R5 doorbell 0x{db:08x}, {'ok' if ok_rpu else 'FAILED'}, posted {mbx.rd('posted')},"
                  f" pidx {mbx.rd('final_pidx')}, {mbx.rd('doorbells')} doorbells;"
                  f" drain {'ok' if ok else 'TIMEOUT'} (cidx {cidx}), {dt * 1e3:.1f} ms incl. host polling")
            return ok_rpu and ok, dt

        ok1, dt1 = produce(ih, DB_H2C_PIDX, f"host buffer A -> {args.mem} 0x{dev:x}")
        ok2, dt2 = produce(ic, DB_C2H_PIDX, f"{args.mem} 0x{dev:x} -> host buffer B") if ok1 else (False, 0)
        got = bytes(mm_b[:nbytes]) if ok2 else b""
        bad = None
        if ok2 and got != pat:
            bad = next(k for k in range(0, nbytes, 4) if got[k:k + 4] != pat[k:k + 4])
        untouched = ok2 and mm_b[nbytes:nbytes + 4096] == b"\xa5" * min(4096, blen - nbytes)
        qa.show_delta(before)
        if ok1 and ok2 and bad is None:
            print(f"  data: B == A over {nbytes} B; the page after the transfer is untouched: {untouched}")
            print(f"  ~{nbytes * 8 / dt1 / 1e9:.2f} Gb/s H2C, ~{nbytes * 8 / dt2 / 1e9:.2f} Gb/s C2H"
                  f" (wall clock incl. mailbox and drain polling)")
            print(f"mm {args.mem}: PASS")
        else:
            if bad is not None:
                print(f"  data: first mismatch at byte 0x{bad:x} (page {bad // 4096}):"
                      f" got 0x{int.from_bytes(got[bad:bad + 4], 'little'):08x},"
                      f" want 0x{int.from_bytes(pat[bad:bad + 4], 'little'):08x}")
            print(f"mm {args.mem}: FAIL")
    finally:
        for bfd, m in bufs:
            m.close()
            os.close(bfd)
        for x in reversed(qs):
            x.close()


def main():
    global qe
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="card BDF without function, e.g. 0000:1b:00")
    sp = ap.add_subparsers(dest="cmd", required=True)
    h = sp.add_parser("h2c")
    h.add_argument("--producer", choices=("host", "rpu"), required=True)
    h.add_argument("--pkt", type=int, default=4096, help="packet bytes, 1..4096 (one descriptor each)")
    h.add_argument("--packets", type=int, default=10000, help="host producer: packets to post")
    h.add_argument("--batch", type=int, default=64, help="host producer: descriptors per doorbell")
    h.add_argument("--ring-idx", type=int, default=0, help="CSR ring-size index (0: 2048 descriptors)")
    h.add_argument("--seed", type=lambda s: int(s, 0), default=0x5EED0000)
    h.add_argument("--params", default="/tmp/rpuq_h2c.json", help="also write the doorbell parameters here")
    h.add_argument("--flow", action="store_true", help="rpu: CIDX from the status writeback via a BDF entry")
    h.add_argument("--bdf-entry", type=int, default=63, help="rpu --flow: BDF entry (window) to use")
    h.add_argument("--bdf-prot", type=lambda s: int(s, 0), default=2, help="rpu --flow: BDF prot")
    h.add_argument("--fn-stride", type=lambda s: int(s, 0), default=0x1_0000,
                   help="rpu: bytes per function in the AXI-side QDMA window (PG347: 64 KiB)")
    m = sp.add_parser("mm", help="host buffer -> HBM/DDR -> host buffer, MM queues started by the R5 (or host)")
    m.add_argument("--producer", choices=("host", "rpu"), default="rpu")
    m.add_argument("--mem", choices=("hbm", "ddr"), default="hbm")
    m.add_argument("--offset", type=lambda s: int(s, 0), default=0x1_0000_0000,
                   help="byte offset into HBM/DDR (default 4 GiB; 4 KiB aligned)")
    m.add_argument("--pages", type=int, default=2047, help="4 KiB pages to move (<= rngsz - 1)")
    m.add_argument("--channel", type=int, choices=(0, 1), help="MM channel (default: V80 placement policy)")
    m.add_argument("--batch", type=int, default=256, help="rpu: descriptors per doorbell write")
    m.add_argument("--ring-idx", type=int, default=0, help="CSR ring-size index (0: 2048 descriptors)")
    m.add_argument("--seed", type=lambda s: int(s, 0), help="data pattern seed (default: from the clock)")
    m.add_argument("--fn-stride", type=lambda s: int(s, 0), default=0x1_0000)
    p = sp.add_parser("rpu-probe", help="read QDMA registers through the R5 path (ELF OP_PROBE)")
    p.add_argument("--addr", type=lambda s: int(s, 0), action="append", help="R5 addresses (default: idents)")
    b = sp.add_parser("rpu-bdf", help="R5 writes one BDF entry, reads it back, clears it (no traffic)")
    b.add_argument("--entry", type=int, default=63)
    b.add_argument("--pa", type=lambda s: int(s, 0), default=0x1_2340_0000, help="test address (never accessed)")
    h.add_argument("--info-only", action="store_true", help="add, start, fill, print the layout, tear down")
    h.add_argument("--probe-layout", action="store_true",
                   help="host producer: after the drain, the R5 reads this queue's H2C PIDX doorbell at the\n"
                        "candidate AXI-window offsets (reads only); compare with the host's PF1 BAR0 view")
    args = ap.parse_args()

    if args.cmd == "rpu-probe":
        mbx = Mbox(args.device)
        mbx.check()
        addrs = args.addr or [R5_QDMA, R5_QDMA + 0x8000, R5_QDMA + 0x1_0000, R5_QDMA + 0x2_0000,
                              R5_QDMA + 0x1_8000, R5_QDMA + 0x2_0000 + 0x8000]
        if mbx.run(OP_PROBE, probe_n=len(addrs), probe_addr=addrs):
            ref = q.Qdma(args.device).rd({"ID": 0})
            print(f"  PF1 BAR0 + 0 (host): 0x{ref.get('ID', 0):08x}")
            fmask = mbx.rd("probe_fmask") if mbx.rd("version") >= 2 else 0
            for k, a in enumerate(addrs):
                v = mbx.rd("probe_val", k)
                where = (f"AXI 0x{0x7_1000_0000 + a - R5_QDMA:x}" if R5_QDMA <= a < R5_QDMA + 0x100_0000 else
                         f"bridge win {(a - R5_HOST) >> 21}" if R5_HOST <= a < R5_QDMA else "PS")
                if fmask >> k & 1:
                    dfsr = mbx.rd("probe_dfsr", k)
                    kind = "SLVERR" if dfsr >> 12 & 1 else "DECERR"
                    print(f"  R5 0x{a:08x} ({where}): ABORT DFSR 0x{dfsr:08x} ({kind})")
                else:
                    print(f"  R5 0x{a:08x} ({where}): 0x{v:08x}"
                          f"{'  <- QDMA ident' if v == QDMA_IDENT else ''}")
        return

    if args.cmd == "rpu-bdf":
        mbx = Mbox(args.device)
        mbx.check()
        names = ["addr_lo", "addr_hi", "pasid", "func", "window", "smid"]
        ok = mbx.run(OP_BDF, bdf_entry=args.entry, pa_lo=args.pa & 0xFFFFFFFF, pa_hi=args.pa >> 32,
                     bdf_perm=3, bdf_prot=2)
        rb = [mbx.rd("bdf_rb", k) for k in range(6)]
        print(f"  set entry {args.entry}: " + ", ".join(f"{n} 0x{v:08x}" for n, v in zip(names, rb)))
        want_lo = args.pa & 0xFFE00000
        ok = ok and rb[0] == want_lo and rb[1] == args.pa >> 32
        ok2 = mbx.run(OP_BDF, bdf_entry=args.entry, pa_lo=0, pa_hi=0)
        rb = [mbx.rd("bdf_rb", k) for k in range(6)]
        print(f"  cleared:   " + ", ".join(f"{n} 0x{v:08x}" for n, v in zip(names, rb)))
        print("rpu-bdf: PASS" if ok and ok2 and not any(rb) else "rpu-bdf: FAIL")
        return

    if args.cmd == "mm":
        return cmd_mm(args)

    lb = q.Lb(Bar(args.device))
    lb.check_id()
    lb.setup("sink", check=True, cmpt=False, seed=args.seed)
    qa = q.Qdma(args.device)
    before = qa.snap()

    qe = ExtQueue(args.ring_idx)
    bfd = mm = None
    try:
        i = qe.info()
        stride = 4096                                   # one page per packet: one descriptor, one page
        bfd, mm = qe.buffer(i.rngsz * stride)
        pkt = q.pattern(args.pkt, args.seed)
        for k in range(i.rngsz):
            mm[k * stride:k * stride + args.pkt] = pkt
        filled = qe.fill(bfd, args.pkt, stride)
        i = qe.info()
        par = dict(qid=qe.qid, qidx=i.qidx, qidx_hw=i.qidx_hw, func_id=i.func_id, qbase=i.qbase,
                   rngsz=i.rngsz, desc_sz=i.desc_sz, desc_bus=f"0x{i.desc_bus:x}", wb_bus=f"0x{i.wb_bus:x}",
                   doorbell_offset=f"0x{DB_H2C_PIDX + DB_STRIDE * i.qidx:x}", pkt=args.pkt, seed=f"0x{args.seed:x}")
        print(f"ext H2C queue {qe.qid}: qidx {i.qidx} (hw {i.qidx_hw}, func {i.func_id}, qbase {i.qbase}),"
              f" ring {i.rngsz} x {i.desc_sz} B at 0x{i.desc_bus:x}, wb at 0x{i.wb_bus:x}"
              f" (pidx {i.hw_pidx}, cidx {i.hw_cidx}); {filled} descriptors filled, {args.pkt} B each")
        if args.info_only:
            return
        if args.producer == "host":
            posted, final, dt = host_produce(DmaBar(args.device), i, args.packets, args.batch)
            print(f"  host producer: {posted} packets, final pidx {final}, {posted * args.pkt * 8 / dt / 1e9:.2f}"
                  f" Gb/s ({dt * 1e3:.1f} ms incl. doorbells)")
        else:
            db = R5_QDMA + args.fn_stride * i.func_id + DB_H2C_PIDX + DB_STRIDE * i.qidx
            par.update(r5_doorbell=f"0x{db:08x}", flow=args.flow)
            with open(args.params, "w") as f:
                json.dump(par, f, indent=1)
            mbx = Mbox(args.device)
            mbx.check()
            if not args.flow and args.packets > i.rngsz - 1:
                sys.exit(f"--packets {args.packets}: without --flow at most rngsz - 1 = {i.rngsz - 1}")
            print(f"  RPU producer: doorbell R5 0x{db:08x}, {args.packets} packets, batch {args.batch},"
                  f" {'CIDX via BDF entry %d' % args.bdf_entry if args.flow else 'one shot'}")
            t0 = time.time()
            ok_rpu = mbx.run(OP_H2C, db_addr=db, rngsz=i.rngsz, count=args.packets, batch=args.batch,
                             flags=1 if args.flow else 0, bdf_entry=args.bdf_entry, pa_lo=i.wb_bus & 0xFFFFFFFF,
                             pa_hi=i.wb_bus >> 32, bdf_prot=args.bdf_prot, bdf_perm=3, start_pidx=i.hw_pidx,
                             stall_limit=0)
            dt = time.time() - t0
            posted, final = mbx.rd("posted"), mbx.rd("final_pidx")
            cyc = mbx.rd("cycles")
            print(f"  RPU: {'ok' if ok_rpu else 'FAILED'}; posted {posted}, final pidx {final}, last cidx"
                  f" {mbx.rd('last_cidx')}, {mbx.rd('doorbells')} doorbells, {mbx.rd('polls')} CIDX polls,"
                  f" {cyc} cycles; host saw {dt * 1e3:.1f} ms")
        ok, cidx = qe.drain(final, 2000)
        print(f"  drain: {'ok' if ok else 'TIMEOUT'}, hw cidx {cidx}, want {final % i.rngsz}")
        if args.probe_layout and args.producer == "host":
            off = DB_STRIDE * i.qidx
            host_db = DmaBar(args.device).rd(DB_H2C_PIDX + off)
            print(f"  host PF1 BAR0 + 0x{DB_H2C_PIDX + off:x} (H2C PIDX doorbell): 0x{host_db:08x}"
                  f" (final pidx {final}); func {i.func_id}, qidx {i.qidx}, qidx_hw {i.qidx_hw}")
            fn, qa_, qh = i.func_id, i.qidx, i.qidx_hw
            cand = [
                ("64K/fn, PF BAR layout (fn*0x10000 + 0x18004 + 0x10*qidx)", 0x1_0000 * fn + DB_H2C_PIDX + off),
                ("32K/fn queue space, qidx (fn*0x8000 + 4 + 0x10*qidx)", 0x8000 * fn + 4 + off),
                ("32K/fn queue space, qidx_hw (fn*0x8000 + 4 + 0x10*qidx_hw)", 0x8000 * fn + 4 + DB_STRIDE * qh),
                ("queue space, no fn, qidx_hw (4 + 0x10*qidx_hw)", 4 + DB_STRIDE * qh),
                ("64K/fn, CPM4 layout (fn*0x10000 + 0x6404 + 0x10*qidx)", 0x1_0000 * fn + 0x6404 + off),
                ("PF BAR layout, no fn (0x18004 + 0x10*qidx_hw)", DB_H2C_PIDX + DB_STRIDE * qh),
            ]
            cand = [(n, a) for n, a in cand if a < 0x100_0000]
            mbx = Mbox(args.device)
            mbx.check()
            addrs = [R5_QDMA + a for _, a in cand]
            if mbx.run(OP_PROBE, probe_n=len(addrs), probe_addr=addrs):
                fmask = mbx.rd("probe_fmask")
                for k, (n, a) in enumerate(cand):
                    v = mbx.rd("probe_val", k)
                    s = "ABORT" if fmask >> k & 1 else f"0x{v:08x}"
                    hit = "  <- matches the host doorbell" if not fmask >> k & 1 and v == host_db and v else ""
                    print(f"  R5 0x{R5_QDMA + a:08x} {n}: {s}{hit}")
    finally:
        if mm is not None:
            mm.close()
        if bfd is not None:
            os.close(bfd)
        qe.close()                       # STOP, DEL; the driver drops the buffer with the queue

    time.sleep(0.01)
    c = q.show_counters(lb, ["H2C_PKTS", "H2C_BYTES", "H2C_BAD_BEATS", "H2C_ERR_PKTS", "ZERO_PKTS",
                             "LONG_PKTS", "H2C_FIRST", "H2C_LAST"])
    qa.show_delta(before)
    print(f"  card {q.card_gbps(c['H2C_BYTES'], c['H2C_FIRST'], c['H2C_LAST']):.2f} Gb/s first to last beat;"
          f" qid seen by the card {lb.rd('LAST_H2C') & 0xFFF}")
    exp = posted if posted is not None else c["H2C_PKTS"]
    good = ok and c["H2C_PKTS"] == exp and c["H2C_BYTES"] == exp * args.pkt and \
        c["H2C_BAD_BEATS"] == 0 and c["H2C_ERR_PKTS"] == 0
    print("h2c ext: PASS" if good else f"h2c ext: FAIL (expected {exp} packets of {args.pkt} B, clean drain)")


if __name__ == "__main__":
    main()
