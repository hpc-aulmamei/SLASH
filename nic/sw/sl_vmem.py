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
"""sl_vmem: user region -> host address translator of the slvmem service-layer RM.

The translator (PF2 BAR2 0x0400_0000) maps 2 MiB pages of the user region's
host aperture (0x208_0000_0000, 32 GiB) onto the 64 windows of the CPM5
slave bridge (AXI 0xE800_0000 + win * 2 MiB); BDF entry `win` maps that
window to a host bus address. The host owns BDF entries 0..62 (WIN_LIMIT
63): entry 63 belongs to the R5 tests. The RM also has host_probe_vm (PF2
BAR2 0x0701_0000), a host_probe whose requests go through the translator, so
its addresses are virtual (0x208_0000_0000 + VA).

  sudo ./sl_vmem.py -d 0000:1b:00 status             # read only
  sudo ./sl_vmem.py -d 0000:1b:00 test               # guided: faults, map, read, write, pause, bw
  sudo ./sl_vmem.py -d 0000:1b:00 test --pages 8 --steps fault0,read
  sudo ./sl_vmem.py -d 0000:1b:00 map --pages 4      # map, wait for 'unmap', unmap (for kernels)
  sudo ./sl_vmem.py -d 0000:1b:00 kernel             # user region: traffic_virt_4 through the translator
  sudo ./sl_vmem.py -d 0000:1b:00 clean              # disable, invalidate the table (after an abort)

Host memory (test, map, kernel):
  --mem hugepages (default): hugepages, their physical addresses written to the
      BDF entries through PF1 BAR2 or xsdb. Needs no IOMMU translation
      (iommu=pt) and sudo sysctl vm.nr_hugepages=256.
  --mem driver: the slash driver allocates the memory and writes the BDF
      entries (SLASH_QDMA_IOCTL_SLV_*, slv_drv.py); works with the IOMMU
      translating too. Needs the slash module with slave-bridge windows.
Every BDF write and every card write to host memory asks for confirmation.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bar import Bar  # noqa: E402
import host_probe as hp  # noqa: E402
import slv_drv  # noqa: E402

SLV = 0x0400_0000
TABLE = 0x1_0000
PROBE_VM = 0x0701_0000
VA_BASE = 0x208_0000_0000
VA_SIZE = 32 << 30
PAGE = hp.PAGE
NVPN = VA_SIZE // PAGE                 # 16384
WIN_BASE = 0xE800_0000
OWNED = 63                             # BDF entries / windows 0..62
SLV_ID = 0x534C564D
VM_BUILD = 102

R = dict(ID=0x00, VERSION=0x04, BUILD=0x08, CAPS=0x0C, CTRL=0x10, STATUS=0x14, WIN_LIMIT=0x18,
         OUTSTANDING=0x1C, W_PENDING=0x20, CLEAR=0x24, FAULT_ADDR=0x28, FAULT_INFO=0x30,
         FAULT_COUNT=0x34, XLAT_W=0x38, XLAT_R=0x3C, WIN_BASE=0x40, VA_BASE=0x48)
ST = ["init_busy", "drained", "fault_valid", "aw_busy", "ar_busy", "w_fault", "r_fault",
      "enabled", "paused"]
CAUSE = {0: "-", 1: "invalid entry", 2: "no read permission", 3: "no write permission",
         4: "window >= WIN_LIMIT", 5: "disabled"}
E_VALID, E_R, E_W = 1, 2, 4
CTRL_EN, CTRL_PAUSE = 1, 2

# VA pages and windows of a test mapping. vpn 0 and 1 are adjacent (a run
# across them crosses pages); 0x3FFF / 0x2AAA / 0x1555 set every vpn bit both
# ways, windows 42 / 21 / 62 / 1 every window bit.
VPNS = [0x0000, 0x0001, 0x3FFF, 0x2AAA, 0x1555, 0x2000, 0x0800, 0x0400]
WINS = [42, 21, 62, 1, 0, 33, 12, 50]
VPN_UNMAPPED = 0x0002


class SlVmem:
    def __init__(self, bar):
        self.bar = bar

    def rd(self, name):
        return self.bar.rd(SLV + R[name])

    def rd64(self, name):
        return self.bar.rd(SLV + R[name]) | self.bar.rd(SLV + R[name] + 4) << 32

    def wr(self, name, val):
        self.bar.wr(SLV + R[name], val)

    def entry(self, vpn):
        return self.bar.rd(SLV + TABLE + 4 * vpn)

    def set_entry(self, vpn, val):
        self.bar.wr(SLV + TABLE + 4 * vpn, val)

    def status(self):
        st = self.rd("STATUS")
        return st, [n for i, n in enumerate(ST) if st >> i & 1]

    def fault(self):
        fi = self.rd("FAULT_INFO")
        return dict(valid=fi >> 31 & 1, write=fi >> 30 & 1, cause=fi >> 24 & 7, len=fi >> 16 & 0xFF,
                    id=fi & 0xFFFF, addr=self.rd64("FAULT_ADDR"), count=self.rd("FAULT_COUNT"), raw=fi)

    def clear(self):
        self.wr("CLEAR", 3)

    def drain(self, timeout_s=1.0):
        """PAUSE, wait for drained; returns True when nothing is in flight"""
        self.wr("CTRL", self.rd("CTRL") | CTRL_PAUSE)
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            if self.rd("STATUS") >> 1 & 1 and self.rd("OUTSTANDING") == 0 and self.rd("W_PENDING") == 0:
                return True
        return False

    def check_id(self):
        ident = self.rd("ID")
        if ident != SLV_ID:
            sys.exit(f"no sl_vmem at BAR2 0x{SLV:08x} (ID 0x{ident:08x}); is the slvmem RM loaded?")


def fmt_entry(e):
    if not e & E_VALID:
        return f"0x{e:08x} invalid"
    return f"0x{e:08x} {'r' if e & E_R else '-'}{'w' if e & E_W else '-'} window {e >> 8 & 0x3F}"


def show_regs(s):
    st, flags = s.status()
    caps = s.rd("CAPS")
    o = s.rd("OUTSTANDING")
    f = s.fault()
    print(f"sl_vmem: ID 0x{s.rd('ID'):08x} version 0x{s.rd('VERSION'):08x} build {s.rd('BUILD')}"
          f"  caps 0x{caps:08x} ({caps & 0xFFFF} entries, {1 << (caps >> 16 & 0xFF) >> 20} MiB pages,"
          f" {caps >> 24} windows)")
    print(f"  CTRL 0x{s.rd('CTRL'):x}  STATUS 0x{st:03x} {' '.join(flags)}  WIN_LIMIT {s.rd('WIN_LIMIT')}")
    print(f"  outstanding: writes {o & 0xFFFF} reads {o >> 16}, W pending {s.rd('W_PENDING')};"
          f" forwarded bursts: writes {s.rd('XLAT_W')} reads {s.rd('XLAT_R')}")
    print(f"  WIN_BASE 0x{s.rd64('WIN_BASE'):x}  VA_BASE 0x{s.rd64('VA_BASE'):x}")
    print(f"  fault: count {f['count']}, info 0x{f['raw']:08x}" +
          (f" ({'write' if f['write'] else 'read'}, {CAUSE.get(f['cause'], f['cause'])}, AxLEN {f['len']},"
           f" ID {f['id']}, address 0x{f['addr']:x})" if f["valid"] else " (none recorded)"))


# ---------------------------------------------------------------- results
class Results:
    def __init__(self):
        self.rows = []

    def check(self, name, ok, detail=""):
        self.rows.append((name, bool(ok), detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f": {detail}" if detail else ""))
        return ok

    def note(self, name, detail):
        self.rows.append((name, None, detail))
        print(f"  [note] {name}: {detail}")

    def summary(self):
        n_fail = sum(1 for _, ok, _ in self.rows if ok is False)
        n_pass = sum(1 for _, ok, _ in self.rows if ok)
        print(f"\n==== summary: {n_pass} pass, {n_fail} fail")
        for name, ok, detail in self.rows:
            if ok is False:
                print(f"  FAIL {name}: {detail}")
        return n_fail == 0


def run_ok(res):
    return not res.get("stuck") and not set(res["flags"]) & {"timeout", "range_err", "param_err"}


# ---------------------------------------------------------------- mapping
class Mapping:
    """N 2 MiB host pages behind BDF entries WINS[i], at VA pages VPNS[i]
    (hugepages + BDF writes here, or a driver buffer + driver windows)"""

    def __init__(self, s, args, n, perm_entry=E_VALID | E_R | E_W, vpns=None, wins=None):
        if not 1 <= n <= OWNED:
            sys.exit(f"--pages must be 1..{OWNED}")
        self.s = s
        self.vpns = vpns or (VPNS + [v for v in range(NVPN) if v not in VPNS and v != VPN_UNMAPPED])[:n]
        self.wins = wins or (WINS + [w for w in range(OWNED) if w not in WINS])[:n]
        self.drv = None
        if getattr(args, "mem", "hugepages") == "driver":
            self.drv = slv_drv.SlvDriver(args.device)
            inf = self.drv.info()
            if not inf["present"]:
                self.drv.close()
                sys.exit(f"{self.drv.node}: the driver has no slave-bridge windows (see dmesg)")
            print(f"driver: {self.drv.node}, IOMMU {'translating' if inf['iommu'] else 'off / pass-through'},"
                  f" windows in use {inf['used'] or 'none'}")
            self.buf = self.drv.buf(n * PAGE)
            self.pages = [(None, self.buf.chunk(i), 0) for i in range(n)]   # DMA address set by map()
        else:
            self.buf = hp.HugeBuf(max(args.hugepages, n), PAGE, keep=n)
            self.pages = self.buf.pages                   # (fd, mm, pa)
        self.win_reg = (3 << 30) | (args.prot << 26) | (PAGE >> 12)
        self.host = None
        self.bdf_done = False
        self.perm = perm_entry
        self.args = args
        self.clr_tcl = os.path.abspath(f"sl_vmem_bdf_clear.{os.getpid()}.tcl")

    def va(self, i, off=0):
        return VA_BASE + self.vpns[i] * PAGE + off

    def mm(self, i):
        return self.pages[i][1]

    def table(self):
        print(f"  {'page':>4}  {'card address (VA)':>18}  {'window':>6}  {'AXI':>10}"
              f"  {'host DMA' if self.drv else 'host physical'}")
        for i, (_, _, pa) in enumerate(self.pages):
            print(f"  {i:4}  0x{self.va(i):016x}  {self.wins[i]:6}  0x{WIN_BASE + self.wins[i] * PAGE:08x}"
                  f"  0x{pa:x}")

    def map(self):
        """BDF entries, then table entries. False if the user stopped."""
        s = self.s
        if self.drv:
            return self.map_driver()
        busy = [w for w in self.wins if self.bdf_entry(w)[4] != 0]
        if busy:
            print(f"  BDF entries {busy} are not empty: someone else uses them; stopping")
            return False
        set_e = [(w, pa, self.win_reg) for w, (_, _, pa) in zip(self.wins, self.pages)]
        clr_e = [(w, 0, 0) for w in self.wins]
        hp.xsdb_script(self.clr_tcl, clr_e)
        self.table()
        if self.host:
            if not hp.ask(f"  Type 'yes' to write BDF entries {sorted(self.wins)} through PF1 BAR2: "):
                return False
            self.bdf_done = True
            if not self.host.program(sorted(set_e)):
                print("  the BDF entries did not read back as written")
                return False
        else:
            set_tcl = os.path.abspath(f"sl_vmem_bdf_set.{os.getpid()}.tcl")
            hp.xsdb_script(set_tcl, sorted(set_e))
            print(f"  Write the BDF entries (as root, another terminal):\n"
                  f"    source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb {set_tcl}")
            self.bdf_done = True
            if not hp.ask("  Type 'yes' when they read back as set: "):
                return False
        for i in range(len(self.pages)):
            s.set_entry(self.vpns[i], self.perm | self.wins[i] << 8)
        bad = [i for i in range(len(self.pages)) if s.entry(self.vpns[i]) != self.perm | self.wins[i] << 8]
        if bad:
            print(f"  table entries of pages {bad} did not read back")
            return False
        return True

    def map_driver(self):
        busy = [w for w in self.wins if w in self.drv.info()["used"] or any(self.bdf_entry(w))]
        if busy:
            print(f"  BDF entries {busy} are not empty: someone else uses them; stopping")
            return False
        if not hp.ask(f"  Type 'yes' to let the driver map windows {sorted(self.wins)} onto its buffer: "):
            return False
        self.bdf_done = True
        for i, w in enumerate(self.wins):
            try:
                self.pages[i] = (None, self.pages[i][1], self.drv.map(w, self.buf, i * PAGE))
            except OSError as e:
                print(f"  SLV_MAP window {w}: {e}")
                return False
        self.table()
        if self.host:
            bad = [w for w, (_, _, d) in zip(self.wins, self.pages)
                   if self.host.entry(w)[:2] != [d & 0xFFFFF000, d >> 32]]
            print(f"  BDF entries read back through PF1 BAR2: {'as mapped' if not bad else f'WRONG {bad}'};"
                  f" BRG_SLV_TZ_ERR_ADDR 0x{self.host.rd(hp.BRG_ERR):08x}")
            if bad:
                return False
        for i in range(len(self.pages)):
            s = self.s
            s.set_entry(self.vpns[i], self.perm | self.wins[i] << 8)
        bad = [i for i in range(len(self.pages)) if self.s.entry(self.vpns[i]) != self.perm | self.wins[i] << 8]
        if bad:
            print(f"  table entries of pages {bad} did not read back")
            return False
        return True

    def unmap_driver(self, drained):
        if self.bdf_done:
            while not (drained or hp.ask("  Type 'yes' to clear the windows anyway: ")):
                pass
            used = self.drv.info()["used"]
            for w in self.wins:
                if w in used:
                    try:
                        self.drv.unmap(w)
                    except OSError as e:
                        print(f"  SLV_UNMAP window {w}: {e}")
        self.pages = [(None, None, d) for (_, _, d) in self.pages]   # drop the chunk views
        self.buf.close()
        self.drv.close()                  # also clears anything still mapped through this fd
        print("  unmapped: table entries invalid, translator disabled, windows cleared by the driver,"
              " buffer released")

    def bdf_entry(self, w):
        return self.host.entry(w) if self.host else [0] * 6

    def unmap(self):
        s = self.s
        drained = s.drain()
        if not drained:
            o = s.rd("OUTSTANDING")
            print(f"  translator did not drain (outstanding writes {o & 0xFFFF} reads {o >> 16},"
                  f" W pending {s.rd('W_PENDING')})")
        for v in self.vpns + [VPN_UNMAPPED]:
            s.set_entry(v, 0)
        s.wr("WIN_LIMIT", OWNED)
        s.wr("CTRL", 0)
        if self.drv:
            return self.unmap_driver(drained)
        if not self.bdf_done:
            self.buf.free_all()
            return
        if not drained:
            print("  the BDF entries stay set and the hugepages stay allocated until you confirm")
        clr_e = [(w, 0, 0) for w in sorted(self.wins)]
        if self.host:
            while not (drained or hp.ask("  Type 'yes' to clear the BDF entries anyway: ")):
                pass
            while not self.host.program(clr_e):
                if not hp.ask("  the BDF entries did not read back as zeros; 'yes' retries, 'no' keeps the"
                              " hugepages and exits: "):
                    print(f"  hugepages kept: clear the entries over JTAG with {self.clr_tcl}")
                    os._exit(1)
        else:
            print(f"  Clear the BDF entries (as root):\n"
                  f"    source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb {self.clr_tcl}")
            while not hp.ask("  Type 'yes' when they read back as zeros: "):
                pass
        self.buf.free_all()
        print("  unmapped: table entries invalid, translator disabled, BDF entries clear, hugepages released")


# ---------------------------------------------------------------- probe helpers
def start(p, cmd, addr, length, beats, outst, seed, timeout_cycles):
    p.wr64("ADDR_LO", addr)
    p.wr("LEN", length)
    p.wr("BEATS", beats)
    p.wr("OUTST", outst)
    p.wr("SEED", seed)
    p.wr("TIMEOUT", timeout_cycles)
    p.wr("CMD", cmd)


def wait(p, timeout_s):
    t_end = time.time() + timeout_s
    while time.time() < t_end:
        if not p.rd("STATUS") & 1:
            return p.result()
    res = p.result()
    res["stuck"] = True
    return res


def brief(res):
    return (f"STATUS 0x{res['status']:x} {' '.join(res['flags'])}; R beats {res['r_beats']}"
            f" RRESP errs {res['rresp_errs']} mismatches {res['mismatches']}; B {res['w_bursts']}"
            f" BRESP errs {res['bresp_errs']}" + (" STUCK" if res.get("stuck") else ""))


def fault_check(rs, s, name, res, write, cause, addr, n_err, brg0, bdfh):
    """a run that must fault: n_err error responses, fault record, nothing at the CPM"""
    f = s.fault()
    if write:
        ok_run = run_ok(res) and res["bresp_errs"] == n_err and res["first_bresp"] == "SLVERR"
    else:
        ok_run = run_ok(res) and res["rresp_errs"] == n_err and res["first_rresp"] == "SLVERR"
    rs.check(f"{name}: SLVERR on the card", ok_run, brief(res))
    ok_rec = (f["valid"] and f["write"] == write and f["cause"] == cause
              and (f["addr"] & (VA_SIZE - 1)) == (addr & (VA_SIZE - 1)))
    rs.check(f"{name}: fault record", ok_rec,
             f"{'write' if f['write'] else 'read'}, cause {f['cause']} ({CAUSE.get(f['cause'])}),"
             f" address 0x{f['addr']:x}, count {f['count']}; expected cause {cause} at 0x{addr:x}")
    if bdfh:
        brg = bdfh.rd(hp.BRG_ERR)
        rs.check(f"{name}: nothing reached the CPM", brg == brg0, f"BRG_SLV_TZ_ERR_ADDR 0x{brg:08x}")
    s.clear()
    return f


# ---------------------------------------------------------------- commands
def cmd_status(bar, args):
    s = SlVmem(bar)
    s.check_id()
    show_regs(s)
    valid = [(v, e) for v in range(NVPN) for e in [s.entry(v)] if e & E_VALID]
    print(f"  table: {len(valid)} valid entries" + (":" if valid else ""))
    for v, e in valid[:32]:
        print(f"    vpn 0x{v:04x} (0x{VA_BASE + v * PAGE:x}): {fmt_entry(e)}")
    for name, base, build in [("host_probe_vm", PROBE_VM, VM_BUILD), ("host_probe", hp.PROBE, None)]:
        p = hp.Probe(bar, base)
        ident = p.rd("ID")
        print(f"{name} at 0x{base:08x}: ID 0x{ident:08x} build {p.rd('BUILD')}"
              + ("" if ident == hp.ID else "  ** not a host_probe **"))
    b = hp.BdfBar(args.device)
    if b.mv is None:
        print(f"BDF table: {b.why}")
        return
    b.check()
    used = [(i, b.entry(i)) for i in range(64)]
    used = [(i, e) for i, e in used if any(e)]
    print(f"BDF entries in use: {len(used)}")
    for i, e in used:
        print(f"  entry {i:2}{' (R5)' if i == 63 else ''}: " + " ".join(f"0x{v:08x}" for v in e))


def cmd_clean(bar, args):
    s = SlVmem(bar)
    s.check_id()
    show_regs(s)
    if not s.drain():
        print("translator does not drain; table left as it is")
        return
    for v in range(NVPN):
        s.set_entry(v, 0)
    s.wr("WIN_LIMIT", OWNED)
    s.wr("CTRL", 0)
    s.clear()
    print("table cleared, translator disabled, fault record cleared")
    print("BDF entries are not touched: host_probe.py bdf --entries 63 [--clear] shows / clears them")


def cmd_map(bar, args):
    s = SlVmem(bar)
    s.check_id()
    if s.rd("CTRL") & CTRL_EN or any(s.entry(v) & E_VALID for v in VPNS):
        sys.exit("the translator is enabled or has entries: run 'clean' first")
    m = Mapping(s, args, args.pages, E_VALID | E_R | (E_W if args.write else 0))
    m.host = hp.bdf_path(args)
    try:
        if not m.map():
            return
        s.wr("WIN_LIMIT", OWNED)
        s.wr("CTRL", CTRL_EN)
        print(f"\nmapped {args.pages} pages ({'read/write' if args.write else 'read only'}), translator"
              " enabled. A user-region kernel addresses them at the card addresses above.")
        while not hp.ask("Type 'unmap' when done: ", "unmap"):
            pass
    finally:
        m.unmap()


def cmd_test(bar, args):
    steps = set(args.steps.split(","))
    rs = Results()
    s = SlVmem(bar)
    pv = hp.Probe(bar, PROBE_VM)
    p0 = hp.Probe(bar, hp.PROBE)
    seed = 0x5EED_0000 | (os.getpid() & 0xFFFF)

    # ---- 1) read-only checks
    print("1) registers (read only)")
    s.check_id()
    show_regs(s)
    st, flags = s.status()
    rs.check("ID / version / caps", s.rd("VERSION") == 0x0001_0000 and s.rd("CAPS") == 0x4015_4000,
             f"0x{s.rd('VERSION'):08x} 0x{s.rd('CAPS'):08x}")
    rs.check("bases", s.rd64("WIN_BASE") == WIN_BASE and s.rd64("VA_BASE") == VA_BASE)
    rs.check("idle after load", not st & 1 and s.rd("OUTSTANDING") == 0 and s.rd("W_PENDING") == 0,
             f"STATUS {' '.join(flags)}")
    clean = s.rd("CTRL") == 0 and s.rd("WIN_LIMIT") == OWNED
    rs.check("reset state (disabled, WIN_LIMIT 63)", clean, f"CTRL {s.rd('CTRL')} WIN_LIMIT {s.rd('WIN_LIMIT')}")
    rs.check("host_probe_vm present", pv.rd("ID") == hp.ID and pv.rd("BUILD") == VM_BUILD,
             f"ID 0x{pv.rd('ID'):08x} build {pv.rd('BUILD')}")
    rs.check("host_probe present", p0.rd("ID") == hp.ID, f"ID 0x{p0.rd('ID'):08x} build {p0.rd('BUILD')}")
    used = [v for v in range(NVPN) if s.entry(v) & E_VALID]
    if not rs.check("table empty", not used, f"{len(used)} valid entries"):
        return rs.summary()
    if not clean or pv.rd("ID") != hp.ID or pv.rd("STATUS") & 1:
        print("stopping: the RM is not in its reset state (run 'clean', or reload the partial)")
        return rs.summary()
    host = hp.bdf_path(args)
    brg0 = host.rd(hp.BRG_ERR) if host else 0
    print(f"  BRG_SLV_TZ_ERR_ADDR before the tests: 0x{brg0:08x}" if host else "")

    pv.window(VA_BASE, VA_SIZE)                  # the probe refuses anything outside the aperture
    pv.wr("CMD", 1 << 31)                        # clear its status
    s.clear()

    def run(cmd, addr, length, beats, outst, sd, timeout_s=1.0):
        return pv.run(cmd, addr, length, beats, outst, sd, timeout_s=timeout_s)

    # ---- 2) faults with nothing mapped (nothing can be forwarded: BDF entries untouched)
    if "fault0" in steps:
        print("\n2) faults with nothing mapped (no BDF entry is written; nothing may leave the translator)")
        a = VA_BASE + 0x1000
        res = run(2, a, 4096, 64, 1, seed)
        fault_check(rs, s, "read, disabled", res, 0, 5, a, 64, brg0, host)
        res = run(1, a, 4096, 64, 1, seed)
        fault_check(rs, s, "write, disabled", res, 1, 5, a, 1, brg0, host)
        s.wr("CTRL", CTRL_EN)
        a = VA_BASE + 0x7_FFE0_0000 + 0x3000         # last page of the aperture
        res = run(2, a, 4096, 64, 4, seed)
        fault_check(rs, s, "read, invalid entry", res, 0, 1, a, 64, brg0, host)
        res = run(1, a, 8192, 64, 2, seed)
        fault_check(rs, s, "write, invalid entry", res, 1, 1, a, 2, brg0, host)
        rs.check("nothing forwarded", s.rd("XLAT_W") == 0 and s.rd("XLAT_R") == 0,
                 f"XLAT_W {s.rd('XLAT_W')} XLAT_R {s.rd('XLAT_R')}")
        s.wr("CTRL", 0)

    if not steps & {"read", "write", "fault", "pause", "bw"}:
        pv.window(0, 0)
        return rs.summary()

    # ---- 3) map
    print(f"\n3) map {args.pages} pages (host hugepages -> BDF entries -> translator table)")
    m = Mapping(s, args, args.pages)
    m.host = host
    N = len(m.pages)
    seeds = [seed ^ (0x0101_0101 * (i + 1)) for i in range(N)]
    writes = False
    try:
        if not m.map():
            print("stopped before any traffic")
            return rs.summary()
        s.wr("CTRL", CTRL_EN)
        s.wr("CLEAR", 2)

        def fill(i, sd):
            m.mm(i)[0:PAGE] = hp.pattern(m.va(i), PAGE, sd)

        def same(i, want):
            return m.mm(i)[0:PAGE] == want

        # ---- 4) reads through the translator
        if "read" in steps:
            print("\n4) READ: the host fills each page, host_probe_vm reads it at its VA")
            for i in range(N):
                fill(i, seeds[i])
            for i in range(N):
                for length, beats, outst in [(64, 1, 1), (PAGE, 64, 32)]:
                    res = run(2, m.va(i), length, beats, outst, seeds[i])
                    rs.check(f"read page {i} (VA 0x{m.va(i):x} -> window {m.wins[i]}) {length} B",
                             run_ok(res) and res["rresp_errs"] == 0 and res["mismatches"] == 0
                             and res["r_beats"] == length // 64, brief(res))
            if N > 1:                                  # vpn 0 and 1: one run across two windows
                fill(0, seed)
                fill(1, seed)
                res = run(2, m.va(0, PAGE - 8192), 16384, 64, 8, seed)
                rs.check("read across VA pages 0/1", run_ok(res) and res["rresp_errs"] == 0
                         and res["mismatches"] == 0, brief(res))
            rs.check("forwarded reads counted", s.rd("XLAT_R") > 0, f"XLAT_R {s.rd('XLAT_R')}")

        # ---- 5) writes through the translator
        if steps & {"write", "fault", "pause"}:
            writes = hp.ask(f"\n5) WRITE: host_probe_vm writes the {N} mapped pages (and the fault tests"
                            " try writes that must be dropped).\n   Type 'yes' to let the card write host"
                            " memory: ")
        if writes and "write" in steps:
            for i in range(N):
                m.mm(i)[0:PAGE] = b"\xCC" * PAGE
            cc = b"\xCC" * PAGE
            res = run(1, m.va(0), 64, 1, 1, seeds[0])
            ok = m.mm(0)[0:64] == hp.pattern(m.va(0), 64, seeds[0]) and m.mm(0)[64:PAGE] == cc[64:]
            ok_other = all(same(j, cc) for j in range(1, N))
            rs.check("write 64 B to page 0", run_ok(res) and res["bresp_errs"] == 0 and ok and ok_other,
                     brief(res) + f"; host: data {'OK' if ok else 'WRONG'},"
                     f" other pages {'untouched' if ok_other else 'CHANGED'}")
            for i in range(N):
                res = run(1, m.va(i), PAGE, 64, 32, seeds[i] ^ 0xFFFF)
                ok = same(i, hp.pattern(m.va(i), PAGE, seeds[i] ^ 0xFFFF))
                ok_other = all(same(j, cc) for j in range(i + 1, N))
                rs.check(f"write page {i} (VA 0x{m.va(i):x} -> window {m.wins[i]})",
                         run_ok(res) and res["bresp_errs"] == 0 and ok and ok_other,
                         brief(res) + f"; host: data {'OK' if ok else 'WRONG'},"
                         f" later pages {'untouched' if ok_other else 'CHANGED'}")
            rs.check("forwarded writes counted", s.rd("XLAT_W") > 0, f"XLAT_W {s.rd('XLAT_W')}")

        # ---- 6) faults with pages mapped
        if "fault" in steps:
            print("\n6) faults with pages mapped (host memory must not change)")
            s.clear()
            if host:
                brg0 = host.rd(hp.BRG_ERR)
            for i in range(N):
                fill(i, seeds[i])
            snap = [bytes(m.mm(i)[0:PAGE]) for i in range(N)]

            def host_same(name):
                bad = [i for i in range(N) if not same(i, snap[i])]
                rs.check(f"{name}: host memory unchanged", not bad, f"changed pages {bad}" if bad else "")

            a = VA_BASE + VPN_UNMAPPED * PAGE + 0x2000
            res = run(2, a, 4096, 64, 1, seed)
            fault_check(rs, s, "read, unmapped page", res, 0, 1, a, 64, brg0, host)
            if writes:
                res = run(1, a, 4096, 64, 1, seed)
                fault_check(rs, s, "write, unmapped page", res, 1, 1, a, 1, brg0, host)
                host_same("write, unmapped page")
            # read-only page: writes dropped, reads pass
            s.set_entry(m.vpns[0], E_VALID | E_R | m.wins[0] << 8)
            if writes:
                res = run(1, m.va(0, 0x1000), 8192, 64, 2, seed ^ 0xA5A5)
                fault_check(rs, s, "write, read-only page", res, 1, 3, m.va(0, 0x1000), 2, brg0, host)
                host_same("write, read-only page")
            res = run(2, m.va(0), PAGE, 64, 32, seeds[0])
            rs.check("read, read-only page", run_ok(res) and res["rresp_errs"] == 0 and res["mismatches"] == 0,
                     brief(res))
            s.set_entry(m.vpns[0], E_VALID | E_R | E_W | m.wins[0] << 8)
            # write-only page: reads fault
            if N > 1:
                s.set_entry(m.vpns[1], E_VALID | E_W | m.wins[1] << 8)
                res = run(2, m.va(1, 0x4000), 4096, 64, 1, seeds[1])
                fault_check(rs, s, "read, write-only page", res, 0, 2, m.va(1, 0x4000), 64, brg0, host)
                s.set_entry(m.vpns[1], E_VALID | E_R | E_W | m.wins[1] << 8)
            # WIN_LIMIT: page 0 (window 42) above the limit, page 1 (window 21) below it
            lim = m.wins[0]
            below = [i for i in range(N) if m.wins[i] < lim]
            s.wr("WIN_LIMIT", lim)
            res = run(2, m.va(0), 4096, 64, 1, seeds[0])
            fault_check(rs, s, f"read, window {lim} >= WIN_LIMIT {lim}", res, 0, 4, m.va(0), 64, brg0, host)
            if writes:
                res = run(1, m.va(0), 4096, 64, 1, seed ^ 0x5A5A)
                fault_check(rs, s, f"write, window {lim} >= WIN_LIMIT {lim}", res, 1, 4, m.va(0), 1, brg0, host)
                host_same("write above WIN_LIMIT")
            if below:
                i = below[0]
                res = run(2, m.va(i), 4096, 64, 1, seeds[i])
                rs.check(f"read, window {m.wins[i]} < WIN_LIMIT {lim}",
                         run_ok(res) and res["rresp_errs"] == 0 and res["mismatches"] == 0, brief(res))
            s.wr("WIN_LIMIT", OWNED)
            # window 63 (the R5's BDF entry) is never reachable: read only
            s.set_entry(VPN_UNMAPPED, E_VALID | E_R | 63 << 8)
            a = VA_BASE + VPN_UNMAPPED * PAGE
            res = run(2, a, 4096, 64, 1, seed)
            fault_check(rs, s, "read, window 63 (R5)", res, 0, 4, a, 64, brg0, host)
            s.set_entry(VPN_UNMAPPED, 0)
            # faults among forwarded traffic: one run over pages 0 (mapped) and 1 (invalid / read-only)
            if N > 1:
                fill(0, seed)
                fill(1, seed)
                s.set_entry(m.vpns[1], 0)
                s.clear()
                res = run(2, m.va(0), 2 * PAGE, 64, 32, seed)
                f = s.fault()
                rs.check("read 4 MiB, second page invalid: first page OK, second page SLVERR",
                         run_ok(res) and res["r_beats"] == 2 * PAGE // 64
                         and res["rresp_errs"] == PAGE // 64 and res["mismatches"] == 0,
                         brief(res) + f"; expected {PAGE // 64} RRESP errors, 0 mismatches")
                rs.note("fault count, mixed read", f"{f['count']} (512 bursts of 4 KiB were refused)")
                s.clear()
                if writes:
                    s.set_entry(m.vpns[1], E_VALID | E_R | m.wins[1] << 8)
                    m.mm(0)[0:PAGE] = b"\xCC" * PAGE
                    m.mm(1)[0:PAGE] = b"\xCC" * PAGE
                    res = run(1, m.va(0), 2 * PAGE, 64, 32, seed ^ 0x3C3C)
                    ok0 = same(0, hp.pattern(m.va(0), PAGE, seed ^ 0x3C3C))
                    ok1 = same(1, b"\xCC" * PAGE)
                    rs.check("write 4 MiB, second page read-only: first written, second untouched",
                             run_ok(res) and res["bresp_errs"] == PAGE // 4096 and ok0 and ok1,
                             brief(res) + f"; host: page 0 {'OK' if ok0 else 'WRONG'},"
                             f" page 1 {'untouched' if ok1 else 'CHANGED'}")
                    f = s.fault()
                    rs.note("fault count, mixed write", f"{f['count']} (expected {PAGE // 4096})")
                    s.clear()
                s.set_entry(m.vpns[1], E_VALID | E_R | E_W | m.wins[1] << 8)
            if host:
                brg0 = host.rd(hp.BRG_ERR)

        # ---- 7) PAUSE and drain
        if "pause" in steps:
            print("\n7) PAUSE: requests wait while paused, in-flight ones drain")
            fill(0, seeds[0])
            s.wr("CTRL", CTRL_EN | CTRL_PAUSE)
            xr = s.rd("XLAT_R")
            start(pv, 2, m.va(0), PAGE, 64, 32, seeds[0], 0)
            time.sleep(0.05)
            st, flags = s.status()
            held = pv.rd("STATUS") & 1 and pv.rd("R_BEATS") == 0 and s.rd("XLAT_R") == xr
            rs.check("paused before the run: nothing forwarded", held and "drained" in flags and "paused" in flags,
                     f"probe busy {pv.rd('STATUS') & 1}, R beats {pv.rd('R_BEATS')}, STATUS {' '.join(flags)}")
            s.wr("CTRL", CTRL_EN)
            res = wait(pv, 2.0)
            rs.check("run completes after PAUSE is cleared", run_ok(res) and res["mismatches"] == 0
                     and res["r_beats"] == PAGE // 64, brief(res))
            # pause in the middle of a long run
            n_rd = 16
            start(pv, 2, m.va(0), PAGE, 1, 32, seeds[0], 0)
            for _ in range(n_rd):
                pv.rd("R_BEATS")
            s.wr("CTRL", CTRL_EN | CTRL_PAUSE)
            t_end = time.time() + 0.5
            drained = False
            while time.time() < t_end and not drained:
                drained = s.rd("STATUS") >> 1 & 1 and s.rd("OUTSTANDING") == 0
            beats1 = pv.rd("R_BEATS")
            time.sleep(0.02)
            beats2 = pv.rd("R_BEATS")
            mid = pv.rd("STATUS") & 1
            detail = (f"drained {int(bool(drained))}, probe busy {mid}, R beats {beats1} then {beats2}"
                      f" of {PAGE // 64}")
            if mid:
                rs.check("PAUSE during a run: drains, then nothing moves", drained and beats1 == beats2, detail)
            else:
                rs.note("PAUSE during a run", "not exercised: the run ended before PAUSE; " + detail)
            s.wr("CTRL", CTRL_EN)
            res = wait(pv, 2.0)
            rs.check("run completes after PAUSE is cleared (mid-run)", run_ok(res) and res["mismatches"] == 0
                     and res["rresp_errs"] == 0 and res["r_beats"] == PAGE // 64, brief(res))

        # ---- 8) bandwidth and latency, translated vs untranslated
        if "bw" in steps:
            print(f"\n8) bandwidth: host_probe_vm at the VA vs host_probe at the window (page 0, {args.bw_reps}"
                  " runs each, Gb/s card side)")
            i = 0
            aw = WIN_BASE + m.wins[i] * PAGE
            p0.window(aw, PAGE)
            p0.wr("CMD", 1 << 31)
            modes = [("read", 2)] + ([("write", 1)] if writes else [])
            print(f"   {'path':12} {'mode':6} {'burst':>6} {'outst':>5}   {'min':>7} {'avg':>7} {'max':>7}")
            for mode, cmd in modes:
                for beats, outst in [(64, 32), (4, 32), (64, 8)]:
                    for name, prb, addr in [("translated", pv, m.va(i)), ("direct", p0, aw)]:
                        rates = []
                        for _ in range(args.bw_reps):
                            if cmd == 2:
                                m.mm(i)[0:PAGE] = hp.pattern(addr, PAGE, seed)
                            res = prb.run(cmd, addr, PAGE, beats, outst, seed)
                            if not run_ok(res) or res["rresp_errs"] or res["bresp_errs"] or res["mismatches"]:
                                rs.check(f"bw {name} {mode} {beats}x{outst}", False, brief(res))
                                raise RuntimeError("bandwidth run failed")
                            rates.append(hp.gbps(PAGE, max(res["w_cycles"], res["r_cycles"])))
                        print(f"   {name:12} {mode:6} {beats * 64:5}B {outst:5}   {min(rates):7.2f}"
                              f" {sum(rates) / len(rates):7.2f} {max(rates):7.2f}")
            for name, prb, addr in [("translated", pv, m.va(i)), ("direct", p0, aw)]:
                m.mm(i)[0:PAGE] = hp.pattern(addr, PAGE, seed)
                res = prb.run(2, addr, 64 * 256, 1, 1, seed)
                mn, mx, sm = res["rd_lat"]
                rs.note(f"read latency, {name}", f"64 B x1: min {mn * 4} ns, avg"
                        f" {sm / max(1, res['r_beats']) * 4:.0f} ns, max {mx * 4} ns")
            p0.window(0, 0)
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nstopped: {e}")
        rs.check("completed without abort", False, str(e))
    finally:
        pv.window(0, 0)
        print("\n9) unmap")
        f = s.fault()
        if f["valid"]:
            rs.note("fault left over", f"0x{f['raw']:08x} at 0x{f['addr']:x}")
        m.unmap()
        show_regs(s)
        if host:
            rs.check("BDF entries cleared", all(not any(host.entry(w)) for w in m.wins))
    return rs.summary()


# ---------------------------------------------------------------- user-region kernel
# The static's default user region has hbm_bandwidth HLS kernels; traffic_virt_4's
# m_axi is the user region's host port (S_QDMA_SLV_BRIDGE, 0x208_0000_0000), its
# control at AXI 0x0202_0058_0000 = PF2 BAR0 + 0x58_0000. One run touches 16M words
# of 256 bits (512 MiB = VA pages 0..255) from hbm_ptr: wr == 0 writes word i = i,
# else it reads every word and XORs bits 31:0 into out_acc. HLS ignores BRESP/RRESP,
# so unmapped pages show up as faults: writes dropped, reads return zeros.
KCTRL = 0x58_0000
K_AP, K_PTR, K_WR, K_ACC = 0x00, 0x10, 0x1C, 0x24
K_WORDS = 0x100_0000
K_PAGES = K_WORDS * 32 // PAGE                      # 256


class Bar0:
    """PF2 BAR0 (user region AXI-Lite, 0x0202_0000_0000): one 64 KiB kernel control block"""

    def __init__(self, bdf, off):
        if bdf.count(".") == 0:
            bdf = bdf + ".2"
        if bdf.count(":") == 1:
            bdf = "0000:" + bdf
        import mmap
        fd = os.open(f"/sys/bus/pci/devices/{bdf}/resource0", os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(fd, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=off)
        self.mv = memoryview(self.mm).cast("I")

    def rd(self, off):
        return self.mv[off >> 2]

    def wr(self, off, val):
        self.mv[off >> 2] = val & 0xFFFFFFFF


def xor_low32(mm):
    """XOR of bits 31:0 of every 32-byte word of a 2 MiB page"""
    import functools
    import operator
    return functools.reduce(operator.xor, memoryview(mm).cast("I")[::8], 0)


def kernel_words(first):
    """bytes of one page as the kernel writes it: word j = first + j (bits 31:0), rest 0"""
    import array
    w = array.array("I", bytes(PAGE))
    w[::8] = array.array("I", range(first, first + PAGE // 32))
    if sys.byteorder != "little":
        w.byteswap()
    return w.tobytes()


def cmd_kernel(bar, args):
    steps = set(args.steps.split(","))
    rs = Results()
    s = SlVmem(bar)
    s.check_id()
    k = Bar0(args.device, args.ctrl)
    ap = k.rd(K_AP)
    print(f"traffic_virt_4 at PF2 BAR0 0x{args.ctrl:x}: ap_ctrl 0x{ap:08x}")
    if not rs.check("kernel present and idle", ap != 0xFFFFFFFF and ap & 4 and not ap & 1, f"ap_ctrl 0x{ap:08x}"):
        return rs.summary()
    k.wr(K_PTR, 0x1234_5000)
    k.wr(K_PTR + 4, 0x208)
    ok = k.rd(K_PTR) == 0x1234_5000 and k.rd(K_PTR + 4) == 0x208
    if not rs.check("kernel argument registers", ok, f"hbm_ptr reads 0x{k.rd(K_PTR + 4):x}_{k.rd(K_PTR):08x}"):
        return rs.summary()
    st, flags = s.status()
    if s.rd("CTRL") != 0 or any(s.entry(v) & E_VALID for v in range(K_PAGES)):
        sys.exit("the translator is enabled or has entries: run 'clean' first")
    host = hp.bdf_path(args)
    brg0 = host.rd(hp.BRG_ERR) if host else 0
    n = args.pages
    vpns = [(i * 37) % K_PAGES for i in range(n)]           # scattered over the 512 MiB, page 0 mapped
    wins = [(i * 29) % OWNED for i in range(n)]              # scattered over windows 0..62
    first_hole = min(set(range(K_PAGES)) - set(vpns))
    hole_va = VA_BASE + first_hole * PAGE
    m = Mapping(s, args, n, vpns=vpns, wins=wins)
    m.host = host
    print(f"kernel test: VA pages 0..{K_PAGES - 1} (512 MiB from 0x{VA_BASE:x}); {n} mapped, the rest fault;"
          f" first unmapped page {first_hole} (0x{hole_va:x})")

    def run(wr):
        s.clear()
        k.wr(K_PTR, VA_BASE & 0xFFFFFFFF)
        k.wr(K_PTR + 4, VA_BASE >> 32)
        k.wr(K_WR, wr)
        t0 = time.time()
        k.wr(K_AP, 1)
        while time.time() - t0 < args.timeout:
            a = k.rd(K_AP)
            if a & 4 and not a & 1:
                return time.time() - t0, k.rd(K_ACC)
        raise RuntimeError(f"kernel still running after {args.timeout} s (ap_ctrl 0x{k.rd(K_AP):08x})")

    def fault_seen(name, write):
        f = s.fault()
        rs.check(f"{name}: unmapped pages faulted, first at the first unmapped page",
                 f["valid"] and f["write"] == write and f["cause"] == 1
                 and (f["addr"] & (VA_SIZE - 1)) == (hole_va & (VA_SIZE - 1)),
                 f"{'write' if f['write'] else 'read'}, cause {f['cause']} ({CAUSE.get(f['cause'])}),"
                 f" first at 0x{f['addr']:x}, count {f['count']}")
        if host:
            brg = host.rd(hp.BRG_ERR)
            rs.check(f"{name}: nothing reached the CPM", brg == brg0, f"BRG_SLV_TZ_ERR_ADDR 0x{brg:08x}")

    try:
        if not m.map():
            print("stopped before any traffic")
            return rs.summary()
        s.wr("CTRL", CTRL_EN)
        s.wr("CLEAR", 2)
        if "read" in steps:
            print("\nREAD: the host fills the mapped pages with random data; the kernel XORs all 512 MiB")
            exp = 0
            for i in range(n):
                m.mm(i)[0:PAGE] = os.urandom(PAGE)
                exp ^= xor_low32(m.mm(i))
            dt, acc = run(1)
            rs.check("kernel read: out_acc = XOR of the mapped pages (unmapped pages read as zeros)", acc == exp,
                     f"out_acc 0x{acc:08x}, expected 0x{exp:08x}; {dt * 1e3:.0f} ms;"
                     f" forwarded read bursts {s.rd('XLAT_R')}")
            fault_seen("kernel read", 0)
        if "write" in steps and hp.ask(f"\nWRITE: the kernel writes 512 MiB from VA 0; {n} pages reach host memory,"
                                       " the rest must be dropped.\n   Type 'yes' to let the card write host memory: "):
            for i in range(n):
                m.mm(i)[0:PAGE] = b"\xCC" * PAGE
            s.wr("CLEAR", 2)
            dt, _ = run(0)
            bad = [i for i in range(n) if m.mm(i)[0:PAGE] != kernel_words(vpns[i] * PAGE // 32)]
            rs.check(f"kernel write: each mapped page holds its own words (VA page -> window -> host page)",
                     not bad, (f"wrong pages {bad[:8]}" if bad else "all pages exact")
                     + f"; {dt * 1e3:.0f} ms; forwarded write bursts {s.rd('XLAT_W')}")
            for i in bad[:2]:
                got = memoryview(m.mm(i)).cast("I")
                print(f"    page {i} (vpn {vpns[i]}, window {wins[i]}): words 0..3 bits 31:0 ="
                      f" {[hex(got[8 * j]) for j in range(4)]}, expected from {vpns[i] * PAGE // 32:#x}")
            fault_seen("kernel write", 1)
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nstopped: {e}")
        rs.check("completed without abort", False, str(e))
    finally:
        print("\nunmap")
        m.unmap()
        show_regs(s)
        if host:
            rs.check("BDF entries cleared", all(not any(host.entry(w)) for w in m.wins))
    return rs.summary()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="BDF of the V80, e.g. 0000:1b:00")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status", help="registers, valid table entries, BDF entries in use (read only)")
    sub.add_parser("clean", help="pause, invalidate every table entry, disable (BDF table untouched)")
    for name in ("test", "map"):
        s = sub.add_parser(name)
        s.add_argument("--pages", type=int, default=4, help=f"2 MiB pages to map (1-{OWNED}, default 4)")
        s.add_argument("--hugepages", type=int, default=64, help="hugepages to allocate (default 64)")
        s.add_argument("--prot", type=lambda x: int(x, 0), default=2, help="BDF prot (default 2)")
        s.add_argument("--bdf", choices=["auto", "host", "jtag"], default="auto",
                       help="BDF table through PF1 BAR2 (host) or xsdb scripts (jtag); default auto")
        s.add_argument("--mem", choices=["hugepages", "driver"], default="hugepages",
                       help="host memory and BDF entries: hugepages + PF1 BAR2 (default), or the slash driver")
    sub.choices["test"].add_argument("--steps", default="fault0,read,write,fault,pause,bw",
                                     help="comma list of fault0, read, write, fault, pause, bw")
    sub.choices["test"].add_argument("--bw-reps", type=int, default=5)
    sub.choices["map"].add_argument("--write", action="store_true", help="map read/write (default read only)")
    s = sub.add_parser("kernel", help="the user region's traffic_virt_4 kernel through the translator")
    s.add_argument("--pages", type=int, default=62, help=f"VA pages to map, of the kernel's 256 (1-{OWNED}, default 62)")
    s.add_argument("--hugepages", type=int, default=128, help="hugepages to allocate (default 128)")
    s.add_argument("--prot", type=lambda x: int(x, 0), default=2, help="BDF prot (default 2)")
    s.add_argument("--bdf", choices=["auto", "host", "jtag"], default="auto")
    s.add_argument("--mem", choices=["hugepages", "driver"], default="hugepages",
                   help="host memory and BDF entries: hugepages + PF1 BAR2 (default), or the slash driver")
    s.add_argument("--steps", default="read,write", help="comma list of read, write")
    s.add_argument("--ctrl", type=lambda x: int(x, 0), default=KCTRL,
                   help="kernel control offset in PF2 BAR0 (default 0x580000: traffic_virt_4)")
    s.add_argument("--timeout", type=float, default=20.0, help="seconds per kernel run")
    args = ap.parse_args()
    if os.geteuid() != 0:
        sys.exit("run as root (BAR2 mapping, /proc/self/pagemap)")
    bar = Bar(args.device)
    if args.cmd in ("test", "kernel"):
        sys.exit(0 if {"test": cmd_test, "kernel": cmd_kernel}[args.cmd](bar, args) else 1)
    {"status": cmd_status, "clean": cmd_clean, "map": cmd_map}[args.cmd](bar, args)


if __name__ == "__main__":
    main()
