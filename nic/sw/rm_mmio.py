#!/usr/bin/env python3
"""rm_mmio of the smart nictest RM: PF2 BAR2 0x0 (host 0x203_0000_0000, R5 0x9C00_0000).

The RM issues single 32-bit accesses into the CPM5 QDMA queue space
(0x7_1000_0000 + 16 MiB: QDMA CSRs and doorbells of every function) through
M_QDMA_SLV_BRIDGE; the smart static (shell/patch_smart.py) routes it. The
hardware refuses any address outside that window (STATUS.range_err).

  sudo ./rm_mmio.py -d 0000:1b:00 status
  sudo ./rm_mmio.py -d 0000:1b:00 scratch                 # BAR write/read of SCRATCH0/1
  sudo ./rm_mmio.py -d 0000:1b:00 rd 0x710010000          # one RM read in the queue space (see below)
  sudo ./rm_mmio.py -d 0000:1b:00 wr 0x710028004 0x10     # one RM write (a doorbell: know what it does)

The doorbell layout (proven from the R5 on the rpuq static):
H2C PIDX of function fn, queue qidx at 0x7_1000_0000 + fn*0x1_0000 + 0x18004 + 0x10*qidx.
rpuq_ext.py h2c --producer rm drives a whole external-producer queue this way.

Reads: the queue space is not a CSR mirror. fn*0x1_0000 + 0 is unmapped there:
the read completes OKAY with 0 and the QDMA logs "Access targeted unmapped
register via queue space pathway" (GLBL_ERR_STAT bit 3), as R5 reads at
0x9800_0000 did on the rpuq static (2026-11-11 and 2026-11-14). Don't read it.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bar import Bar  # noqa: E402  (single 32-bit BAR loads and stores)

BASE = 0x0000_0000              # rm_mmio in PF2 BAR2
ID = 0x524D_494F                # "RMIO"
QSPC, QSPC_SIZE = 0x7_1000_0000, 0x100_0000
R = dict(ID=0x000, VERSION=0x004, BUILD=0x008, CAPS=0x00C, CMD=0x010, STATUS=0x014, SCRATCH0=0x018,
         SCRATCH1=0x01C, ADDR_LO=0x020, ADDR_HI=0x024, WDATA=0x028, RDATA=0x02C, TIMEOUT=0x030,
         AXI_ATTR=0x034, N_WRITES=0x038, N_READS=0x03C, N_ERRS=0x040, LAT_LAST=0x044, WIN_BASE_LO=0x048,
         WIN_BASE_HI=0x04C, WIN_SIZE=0x050, N_LITE_WR=0x054, CYCLES=0x058)
ST_BITS = ["busy", "done", "resp_err", "timeout", "range_err", "param_err"]
HZ = 250e6                      # service_clk
RESP = {0: "OKAY", 1: "EXOKAY", 2: "SLVERR", 3: "DECERR"}


class RmMmio:
    def __init__(self, bar):
        self.bar = bar

    def rd(self, name):
        return self.bar.rd(BASE + R[name])

    def wr(self, name, v):
        self.bar.wr(BASE + R[name], v)

    def check_id(self):
        v = self.rd("ID")
        if v != ID:
            sys.exit(f"rm_mmio: ID 0x{v:08x} at PF2 BAR2 0x{BASE:x}, want 0x{ID:08x}"
                     " (is the smart nictest RM loaded?)")

    @staticmethod
    def status_str(st):
        bits = [n for i, n in enumerate(ST_BITS) if st >> i & 1]
        return f"0x{st:08x} ({' '.join(bits) or 'idle'}; last resp {RESP[st >> 8 & 3]})"

    def access(self, addr, write, value=0, timeout=0.1):
        """one 32-bit access by the RM; returns (ok, status, rdata, latency cycles)"""
        if not (QSPC <= addr and addr + 4 <= QSPC + QSPC_SIZE and addr % 4 == 0):
            raise ValueError(f"0x{addr:x} is outside the queue space 0x{QSPC:x} + 16 MiB or misaligned")
        st = self.rd("STATUS")
        if st & 1:
            raise RuntimeError(f"rm_mmio busy: {self.status_str(st)}")
        self.wr("ADDR_LO", addr & 0xFFFF_FFFF)
        self.wr("ADDR_HI", addr >> 32)
        if write:
            self.wr("WDATA", value)
        self.wr("CMD", 1 if write else 2)
        t0 = time.time()
        while True:
            st = self.rd("STATUS")
            if not st & 1 and st & 2:
                break
            if time.time() - t0 > timeout:
                return False, st, None, None
        ok = (st & 0x3C) == 0
        return ok, st, (None if write else self.rd("RDATA")), self.rd("LAT_LAST")

    def write(self, addr, value):
        ok, st, _, lat = self.access(addr, True, value)
        if not ok:
            raise RuntimeError(f"rm_mmio write 0x{addr:x}: {self.status_str(st)}")
        return lat

    def read(self, addr):
        ok, st, v, lat = self.access(addr, False)
        if not ok:
            raise RuntimeError(f"rm_mmio read 0x{addr:x}: {self.status_str(st)}")
        return v, lat


def show(m):
    for k in ("ID", "VERSION", "BUILD", "CAPS"):
        print(f"  {k:10s} 0x{m.rd(k):08x}")
    print(f"  STATUS     {m.status_str(m.rd('STATUS'))}")
    win = m.rd("WIN_BASE_HI") << 32 | m.rd("WIN_BASE_LO")
    print(f"  window     0x{win:x} + 0x{m.rd('WIN_SIZE'):x}; AXI_ATTR 0x{m.rd('AXI_ATTR'):02x}")
    print(f"  addr       0x{m.rd('ADDR_HI') << 32 | m.rd('ADDR_LO'):x}, wdata 0x{m.rd('WDATA'):08x},"
          f" rdata 0x{m.rd('RDATA'):08x}")
    print(f"  writes {m.rd('N_WRITES')}, reads {m.rd('N_READS')}, errors {m.rd('N_ERRS')},"
          f" last latency {m.rd('LAT_LAST')} cycles ({m.rd('LAT_LAST') / HZ * 1e9:.0f} ns)")
    print(f"  scratch0 0x{m.rd('SCRATCH0'):08x}, scratch1 0x{m.rd('SCRATCH1'):08x},"
          f" AXI-Lite writes {m.rd('N_LITE_WR')}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="card BDF without function, e.g. 0000:1b:00")
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("status")
    s = sp.add_parser("scratch")
    s.add_argument("--value", type=lambda x: int(x, 0), default=0xC0DE_5EED)
    r = sp.add_parser("rd")
    r.add_argument("addr", type=lambda x: int(x, 0))
    w = sp.add_parser("wr")
    w.add_argument("addr", type=lambda x: int(x, 0))
    w.add_argument("value", type=lambda x: int(x, 0))
    sp.add_parser("clear")
    args = ap.parse_args()

    m = RmMmio(Bar(args.device))
    m.check_id()
    if args.cmd == "status":
        show(m)
    elif args.cmd == "clear":
        m.wr("CMD", 0x8000_0000)
        show(m)
    elif args.cmd == "scratch":
        a, b = args.value, args.value ^ 0xFFFF_FFFF
        m.wr("SCRATCH0", a)
        m.wr("SCRATCH1", b)
        ra, rb = m.rd("SCRATCH0"), m.rd("SCRATCH1")
        print(f"  scratch0 0x{ra:08x} (want 0x{a:08x}), scratch1 0x{rb:08x} (want 0x{b:08x})")
        print("scratch: PASS" if (ra, rb) == (a, b) else "scratch: FAIL")
    elif args.cmd == "rd":
        v, lat = m.read(args.addr)
        print(f"  0x{args.addr:x}: 0x{v:08x} ({lat} cycles)")
    elif args.cmd == "wr":
        lat = m.write(args.addr, args.value)
        print(f"  0x{args.addr:x} <- 0x{args.value:08x} ({lat} cycles to BRESP OKAY)")


if __name__ == "__main__":
    main()
