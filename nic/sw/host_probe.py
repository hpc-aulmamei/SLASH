#!/usr/bin/env python3
"""Card -> host memory probe (build 14 on): host_probe at PF2 BAR2 0x0700_0000.

host_probe is an AXI4 master in the service layer that reaches the host
through M_QDMA_SLV_BRIDGE -> static NoC -> CPM5 slave bridge, region 0 at AXI
0xE800_0000. Its translation (the BDF table, CPM5_DMA1_CSR 0xFCEA_2420 +
0x20 per entry) is set by nobody: it is written over JTAG (xsdb) for this
test. The IOMMU of the host is off, so the entries point at physical
addresses; the probe is limited (WIN_BASE/WIN_SIZE) to the mapped range.

NIC static (default, --windows N, 1-64): region 0 is 128 MiB in 64 windows
of 2 MiB; BDF entry i maps AXI 0xE800_0000 + i * 2 MiB. The session takes N
hugepages (any 2 MiB page), writes an xsdb script for entries 0..N-1 and
tests the N * 2 MiB range, window boundaries included.

m05 static (--windows 0): one window, of which the CPM5 decodes only 4 KiB
(build 14 on hardware: beyond 0xE800_0FFF every read got DECERR); the
session uses one 128 MiB aligned hugepage and stays inside --win.

  sudo ./host_probe.py -d 0000:1b:00 status
  sudo ./host_probe.py -d 0000:1b:00 session             # guided: read, write, bandwidth
  sudo ./host_probe.py -d 0000:1b:00 session --windows 4 --steps read
  sudo ./host_probe.py -d 0000:1b:00 session --windows 0  # m05 static

The session needs hugepages: sudo sysctl vm.nr_hugepages=256 (512 MiB, freed
again with vm.nr_hugepages=0). Every write step asks for confirmation.
"""
import argparse
import mmap
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bar import Bar  # noqa: E402  (single 32-bit BAR loads and stores)

PROBE = 0x0700_0000
SERVICE_HZ = 250e6
PAGE = 2 << 20
ALIGN = 128 << 20              # region size of the CPM5 slave region 0xE800_0000
AXI_BASE = 0xE800_0000         # CPM5 slave region (BDF entry 0)
BDF0 = 0xFCEA_2420             # CPM5_DMA1_CSR bdf_lookup_entry_*0
BRG_ERR = 0xFCEA_0E88          # CPM5_DMA1_CSR BRG_SLV_TZ_ERR_ADDR
HUGEDIR = "/dev/hugepages"

ID = 0x48505242
R = dict(ID=0x000, VERSION=0x004, BUILD=0x008, CAPS=0x00C, CMD=0x010, STATUS=0x014,
         ADDR_LO=0x020, ADDR_HI=0x024, LEN=0x028, BEATS=0x02C, OUTST=0x030, SEED=0x034,
         TIMEOUT=0x038, AXI_ATTR=0x03C, WIN_BASE_LO=0x040, WIN_BASE_HI=0x044,
         WIN_SIZE_LO=0x048, WIN_SIZE_HI=0x04C,
         W_CYC=0x060, R_CYC=0x068, W_BURSTS=0x070, R_BEATS=0x074, BRESP_ERRS=0x078,
         RRESP_ERRS=0x07C, MISMATCHES=0x080, FIRST_RESP=0x084, MM_ADDR=0x088,
         MM_EXP=0x090, MM_GOT=0x094, RD_LAT_MIN=0x0A0, RD_LAT_MAX=0x0A4, RD_LAT_SUM=0x0A8,
         WR_LAT_MIN=0x0B0, WR_LAT_MAX=0x0B4, WR_LAT_SUM=0x0B8, CYCLES=0x0C0)
ST_BITS = ["busy", "done", "bresp_err", "rresp_err", "mismatch", "timeout",
           "range_err", "aborted", "param_err", "w_act", "r_act"]
RESP = {0: "OKAY", 1: "EXOKAY", 2: "SLVERR", 3: "DECERR"}


class Probe:
    def __init__(self, bar):
        self.bar = bar

    def rd(self, name):
        return self.bar.rd(PROBE + R[name])

    def rd64(self, name):
        lo = self.bar.rd(PROBE + R[name])
        hi = self.bar.rd(PROBE + R[name] + 4)
        return (hi << 32) | lo

    def wr(self, name, val):
        self.bar.wr(PROBE + R[name], val)

    def wr64(self, name, val):
        self.bar.wr(PROBE + R[name], val & 0xFFFFFFFF)
        self.bar.wr(PROBE + R[name] + 4, val >> 32)

    def check_id(self):
        ident = self.rd("ID")
        if ident != ID:
            sys.exit(f"no host_probe at BAR2 0x{PROBE:08x} (ID 0x{ident:08x}); is build 14 loaded?")

    def status(self):
        st = self.rd("STATUS")
        flags = [n for i, n in enumerate(ST_BITS) if st >> i & 1]
        return st, flags, (st >> 16) & 0x3F, (st >> 24) & 0x3F

    def window(self, base, size):
        self.wr64("WIN_BASE_LO", base)
        self.wr64("WIN_SIZE_LO", size)

    def run(self, cmd, addr, length, beats, outst, seed, timeout_s=1.0):
        st, flags, _, _ = self.status()
        if "busy" in flags:
            raise RuntimeError(f"probe busy before start (STATUS 0x{st:08x} {flags})")
        self.wr64("ADDR_LO", addr)
        self.wr("LEN", length)
        self.wr("BEATS", beats)
        self.wr("OUTST", outst)
        self.wr("SEED", seed)
        self.wr("TIMEOUT", int(timeout_s * SERVICE_HZ))
        self.wr("CMD", cmd)
        t_end = time.time() + timeout_s + 2.0
        while True:
            st, flags, wo, ro = self.status()
            if "busy" not in flags:
                return self.result()
            if time.time() > t_end:
                res = self.result()
                res["stuck"] = True
                return res

    def result(self):
        st, flags, wo, ro = self.status()
        fr = self.rd("FIRST_RESP")
        res = dict(status=st, flags=flags, w_out=wo, r_out=ro,
                   w_cycles=self.rd64("W_CYC"), r_cycles=self.rd64("R_CYC"),
                   w_bursts=self.rd("W_BURSTS"), r_beats=self.rd("R_BEATS"),
                   bresp_errs=self.rd("BRESP_ERRS"), rresp_errs=self.rd("RRESP_ERRS"),
                   mismatches=self.rd("MISMATCHES"),
                   first_bresp=RESP[fr & 3], first_rresp=RESP[(fr >> 8) & 3],
                   mm_addr=self.rd64("MM_ADDR"), mm_exp=self.rd("MM_EXP"), mm_got=self.rd("MM_GOT"),
                   rd_lat=(self.rd("RD_LAT_MIN"), self.rd("RD_LAT_MAX"), self.rd64("RD_LAT_SUM")),
                   wr_lat=(self.rd("WR_LAT_MIN"), self.rd("WR_LAT_MAX"), self.rd64("WR_LAT_SUM")))
        return res


def pattern(axi_addr, length, seed):
    """Bytes the probe writes / expects at axi_addr.. (32-bit lanes: a[31:0] ^ seed)."""
    import array
    n = length // 4
    a0 = axi_addr & 0xFFFFFFFF
    if a0 + 4 * n > 1 << 32:
        return struct.pack(f"<{n}I", *(((a0 + 4 * i) & 0xFFFFFFFF) ^ seed for i in range(n)))
    words = array.array("I", range(a0, a0 + 4 * n, 4))
    if sys.byteorder != "little":
        words.byteswap()
    data = bytearray(words.tobytes())
    for k in range(4):                         # XOR each byte lane with its seed byte
        sb = (seed >> (8 * k)) & 0xFF
        if sb:
            data[k::4] = data[k::4].translate(bytes(b ^ sb for b in range(256)))
    return bytes(data)


def gbps(nbytes, cycles):
    return nbytes * 8 / (cycles / SERVICE_HZ) / 1e9 if cycles else 0.0


def show(res, nbytes=None, what=""):
    st = res["status"]
    print(f"  {what}STATUS 0x{st:08x} {' '.join(res['flags'])}"
          + ("  ** STUCK (still busy) **" if res.get("stuck") else ""))
    if res["w_bursts"] or res["w_cycles"]:
        mn, mx, sm = res["wr_lat"]
        avg = sm / res["w_bursts"] if res["w_bursts"] else 0
        print(f"    write: {res['w_bursts']} bursts, {res['w_cycles']} cycles"
              + (f", {gbps(nbytes, res['w_cycles']):.2f} Gb/s" if nbytes else "")
              + f"; latency AW->B min {mn} max {mx} avg {avg:.0f} cycles ({avg / SERVICE_HZ * 1e9:.0f} ns)"
              + f"; BRESP errors {res['bresp_errs']} (first {res['first_bresp']})")
    if res["r_beats"] or res["r_cycles"]:
        mn, mx, sm = res["rd_lat"]
        print(f"    read : {res['r_beats']} beats, {res['r_cycles']} cycles"
              + (f", {gbps(nbytes, res['r_cycles']):.2f} Gb/s" if nbytes else "")
              + f"; latency AR->R min {mn} max {mx}"
              + f"; RRESP errors {res['rresp_errs']} (first {res['first_rresp']})"
              + f"; mismatches {res['mismatches']}")
        if res["mismatches"]:
            print(f"    first mismatch at AXI 0x{res['mm_addr']:x}: expected 0x{res['mm_exp']:08x}"
                  f" got 0x{res['mm_got']:08x}")
    if res["w_out"] or res["r_out"]:
        print(f"    outstanding now: writes {res['w_out']} reads {res['r_out']}")


# ---------------------------------------------------------------- host buffer
class HugeBuf:
    """2 MiB hugepages from hugetlbfs, physical address from /proc/self/pagemap."""

    def __init__(self, count, align, keep=1):
        self.pages = []
        self.keep = None
        stop = None
        for i in range(count):
            path = os.path.join(HUGEDIR, f"host_probe.{os.getpid()}.{i}")
            fd = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
            os.unlink(path)
            try:
                os.ftruncate(fd, PAGE)         # a new hugetlbfs file is empty: mmap needs the size
                mm = mmap.mmap(fd, PAGE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
                mm[0:8] = b"\0" * 8            # fault the page in
            except (OSError, ValueError, BufferError) as e:
                os.close(fd)
                stop = e                       # out of hugepages (or a real error, shown below)
                break
            self.pages.append((fd, mm, self.phys(mm)))
        n = len(self.pages)
        cand = [p for p in self.pages if p[2] % align == 0]
        if not cand:
            got = ", ".join(f"0x{p[2]:x}" for p in self.pages[:8])
            self.free_all()
            why = f"; allocation stopped at page {n}: {type(stop).__name__}: {stop}" if stop else ""
            raise RuntimeError(f"no {align >> 20} MiB aligned hugepage among {n} "
                               f"(first: {got}){why}; raise vm.nr_hugepages")
        if len(cand) < keep:
            self.free_all()
            why = f"; allocation stopped at page {n}: {type(stop).__name__}: {stop}" if stop else ""
            raise RuntimeError(f"need {keep} hugepages, got {len(cand)}{why}; raise vm.nr_hugepages")
        print(f"hugepages: {n} allocated, {len(cand)} of them {align >> 20} MiB aligned, keeping {keep}")
        kept = cand[:keep]
        for p in self.pages:                   # give the others back
            if p not in kept:
                p[1].close()
                os.close(p[0])
        self.pages = kept
        self.keep = kept[0]
        self.fd, self.mm, self.pa = self.keep
        for _, mm, pa in self.pages:
            if self.phys(mm) != pa:
                raise RuntimeError("hugepage moved")

    @staticmethod
    def phys(mm):
        import ctypes
        va = ctypes.addressof(ctypes.c_char.from_buffer(mm))
        with open("/proc/self/pagemap", "rb") as f:
            f.seek((va // 4096) * 8)
            ent = struct.unpack("<Q", f.read(8))[0]
        if not ent >> 63 & 1:
            raise RuntimeError("page not present")
        pfn = ent & ((1 << 55) - 1)
        if pfn == 0:
            raise RuntimeError("pagemap PFN 0: run as root")
        return pfn * 4096

    def free_all(self):
        for fd, mm, _ in self.pages:
            mm.close()
            os.close(fd)
        self.pages = []


# ---------------------------------------------------------------- xsdb commands
def xsdb_set(pa, perm, prot, win):
    # window: access_perm [31:30] (3 = read/write), prot [28:26], win_size [25:0] in 4 KiB
    # units. An entry with prot[1] = 1 admits non-secure AxPROT when bits 2 and 0 match
    # (the probe sends AxPROT 010); secure accesses pass any entry (PG347 BDF Table).
    window = (perm << 30) | (prot << 26) | (win >> 12)
    regs = [pa & 0xFFFFF000, pa >> 32, 0, 0, window, 0]
    lines = ["source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb",
             "xsdb% connect",
             'xsdb% targets -set -nocase -filter {name =~ "*PMC*"}',
             f"xsdb% mrd -force 0x{BDF0:08X} 6          ;# entry 0 before (expect 0s)"]
    names = ["addr_lo", "addr_hi", "pasid", "func (PF0)", f"window ({win} B)", "smid (written last)"]
    for i, (v, n) in enumerate(zip(regs, names)):
        lines.append(f"xsdb% mwr -force 0x{BDF0 + 4 * i:08X} 0x{v:08X}   ;# {n}")
    lines += [f"xsdb% mrd -force 0x{BDF0:08X} 6", f"xsdb% mrd -force 0x{BRG_ERR:08X} 1   ;# BRG_SLV_TZ_ERR_ADDR",
              "xsdb% exit"]
    return lines


def xsdb_script(path, entries):
    """xsdb script writing BDF entries [(index, pa, window reg)], pa 0 = clear"""
    L = ["connect", 'targets -set -nocase -filter {name =~ "*PMC*"}']
    for i, pa, window in entries:
        base = BDF0 + 0x20 * i
        regs = [pa & 0xFFFFF000, pa >> 32, 0, 0, window, 0]   # smid (offset 0x14) written last
        L += [f"mwr -force 0x{base + 4 * k:08X} 0x{v:08X}" for k, v in enumerate(regs)]
    first, last = entries[0][0], entries[-1][0]
    L += [f'puts "entry {first}: [mrd -force -value 0x{BDF0 + 0x20 * first:08X} 6]"',
          f'puts "entry {last}: [mrd -force -value 0x{BDF0 + 0x20 * last:08X} 6]"',
          f'puts "BRG_SLV_TZ_ERR_ADDR: [mrd -force -value 0x{BRG_ERR:08X} 1]"', "exit"]
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")


def xsdb_clear():
    lines = ["source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb", "xsdb% connect",
             'xsdb% targets -set -nocase -filter {name =~ "*PMC*"}']
    for i in range(6):
        lines.append(f"xsdb% mwr -force 0x{BDF0 + 4 * i:08X} 0x00000000")
    lines += [f"xsdb% mrd -force 0x{BDF0:08X} 6", f"xsdb% mrd -force 0x{BRG_ERR:08X} 1", "xsdb% exit"]
    return lines


def ask(prompt, need="yes"):
    try:
        ans = input(prompt).strip().lower()
    except EOFError:
        ans = ""
    return ans == need


# ---------------------------------------------------------------- commands
def cmd_status(p, args):
    p.check_id()
    caps = p.rd("CAPS")
    print(f"host_probe: version 0x{p.rd('VERSION'):08x} build {p.rd('BUILD')}  beat {caps & 0xFFFF} B,"
          f" max outstanding {(caps >> 16) & 0xFF}, max burst {(caps >> 24) & 0xFF} beats")
    print(f"  window 0x{p.rd64('WIN_BASE_LO'):x} + 0x{p.rd64('WIN_SIZE_LO'):x}  addr 0x{p.rd64('ADDR_LO'):x}"
          f" len {p.rd('LEN')} beats {p.rd('BEATS')} outstanding {p.rd('OUTST')} seed 0x{p.rd('SEED'):08x}")
    show(p.result())


def bad(res, keys):
    return res.get("stuck") or bool(set(res["flags"]) & keys)


def cmd_session_nic(p, args):
    """NIC static: N BDF entries, 2 MiB windows at AXI_BASE + i * 2 MiB"""
    steps = set(args.steps.split(","))
    N = args.windows
    if not 1 <= N <= 64:
        sys.exit("--windows must be 1..64 (region 0: 64 windows of 2 MiB)")
    W = N * PAGE
    if p.rd64("WIN_SIZE_LO") != 0:
        print("note: probe window already set; it is reprogrammed for this session")
    p.window(0, 0)
    buf = HugeBuf(max(args.hugepages, N), PAGE, keep=N)
    pages = buf.pages                          # (fd, mm, pa); page i <-> AXI_BASE + i * PAGE
    win_reg = (args.perm << 30) | (args.prot << 26) | (PAGE >> 12)
    set_tcl = os.path.abspath(f"host_probe_bdf_set.{os.getpid()}.tcl")
    clr_tcl = os.path.abspath(f"host_probe_bdf_clear.{os.getpid()}.tcl")
    xsdb_script(set_tcl, [(i, pa, win_reg) for i, (_, _, pa) in enumerate(pages)])
    xsdb_script(clr_tcl, [(i, 0, 0) for i in range(N)])
    print(f"host buffer: {N} hugepages, physical " +
          ", ".join(f"0x{pa:x}" for _, _, pa in pages[:4]) + (" ..." if N > 4 else ""))
    print(f"probe AXI 0x{AXI_BASE:x} .. 0x{AXI_BASE + W - 1:x} ({N} x 2 MiB)")
    print(f"\n1) Program BDF entries 0..{N - 1} (as root, in another terminal):\n")
    print(f"   source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb {set_tcl}")
    print(f"   (entry 0 and {N - 1} are read back at the end: addr_lo/hi = the page, window 0x{win_reg:08X})")
    if not ask("\nType 'yes' when the entries are written and read back as set: "):
        print("stopped; nothing was sent to the card")
        buf.free_all()
        return

    def fill(seed):
        for i, (_, mm, _) in enumerate(pages):
            mm[0:PAGE] = pattern(AXI_BASE + i * PAGE, PAGE, seed)

    def host_check(length, seed):
        """data correct in [0, length), 0xCC after it"""
        data_ok = guard_ok = True
        for i, (_, mm, _) in enumerate(pages):
            lo = i * PAGE
            n = max(0, min(PAGE, length - lo))
            if n and mm[0:n] != pattern(AXI_BASE + lo, n, seed):
                data_ok = False
            if n < PAGE and mm[n:PAGE] != b"\xCC" * (PAGE - n):
                guard_ok = False
        return data_ok, guard_ok

    p.window(AXI_BASE, W)                      # the probe refuses anything outside it
    seed = 0x5EED_0000 | (os.getpid() & 0xFFFF)
    ok = True
    try:
        if "read" in steps:
            print(f"\n2) READ test: host fills the {N} pages with the pattern, the probe reads and compares")
            fill(seed)
            runs = [(AXI_BASE, 64, 1, 1), (AXI_BASE, 4096, 64, 1)]
            if N > 1:                          # across the window 0 / window 1 boundary
                runs.append((AXI_BASE + PAGE - 4096, 8192, 64, 8))
            runs += [(AXI_BASE + (N - 1) * PAGE, PAGE, 64, 32), (AXI_BASE, W, 64, 32)]
            for addr, length, beats, outst in runs:
                res = p.run(2, addr, length, beats, outst, seed, timeout_s=max(1.0, W / 2e8))
                show(res, length, f"read 0x{addr:x} +{length} B, {beats * 64} B x{outst}: ")
                if bad(res, {"rresp_err", "mismatch", "timeout", "range_err", "param_err"}):
                    ok = False
                    print("  read test failed: no write test will run")
                    break
        if ok and "write" in steps:
            print(f"\n3) WRITE test: the probe writes 64 B, then all {N * 2} MiB, from AXI 0x{AXI_BASE:x};"
                  " the rest stays 0xCC")
            if ask("   Type 'yes' to let the card write host memory: "):
                for length, s2 in [(64, seed ^ 0xFFFF), (W, seed ^ 0xFF00)]:
                    for _, mm, _ in pages:
                        mm[0:PAGE] = b"\xCC" * PAGE
                    res = p.run(1, AXI_BASE, length, min(64, length // 64), 32 if length > 64 else 1, s2,
                                timeout_s=max(1.0, W / 2e8))
                    show(res, length, f"write {length} B: ")
                    data_ok, guard_ok = host_check(length, s2)
                    print(f"  host check: data {'OK' if data_ok else 'WRONG'}, guard {'OK' if guard_ok else 'CHANGED'}")
                    if not data_ok or not guard_ok or bad(res, {"bresp_err", "timeout", "range_err", "param_err"}):
                        ok = False
                        print("  write test failed: stopping")
                        break
            else:
                print("   write test skipped")
                steps.discard("write")
        if ok and "bw" in steps:
            modes = [("read", 2)] + ([("write", 1), ("both", 7)] if "write" in steps else [])
            fill(seed)
            print(f"\n4) {N * 2} MiB runs, {args.bw_reps} each (Gb/s, card side; host_probe clock"
                  f" {SERVICE_HZ / 1e6:.0f} MHz x 64 B = {SERVICE_HZ * 512 / 1e9:.0f} Gb/s at most)")
            print(f"   {'mode':6} {'burst':>6} {'outst':>5}   {'min':>7} {'avg':>7} {'max':>7}")
            for mode, cmd in modes:
                for beats in (4, 64):
                    for outst in (8, 32):
                        rates = []
                        for _ in range(args.bw_reps):
                            res = p.run(cmd, AXI_BASE, W, beats, outst, seed, timeout_s=max(1.0, W / 1e8))
                            if bad(res, {"bresp_err", "rresp_err", "mismatch", "timeout"}):
                                show(res, W, f"{mode} {beats}x{outst}: ")
                                raise RuntimeError("run failed")
                            cyc = max(res["w_cycles"], res["r_cycles"])
                            rates.append(gbps(W * (2 if cmd == 7 else 1), cyc))
                        print(f"   {mode:6} {beats * 64:5}B {outst:5}   {min(rates):7.2f}"
                              f" {sum(rates) / len(rates):7.2f} {max(rates):7.2f}")
            res = p.run(2, AXI_BASE, 4096, 1, 1, seed)
            mn, mx, sm = res["rd_lat"]
            print(f"   read latency (64 B, 1 outstanding): min {mn / SERVICE_HZ * 1e9:.0f} ns,"
                  f" avg {sm / max(1, res['r_beats']) / SERVICE_HZ * 1e9:.0f} ns, max {mx / SERVICE_HZ * 1e9:.0f} ns")
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nstopped: {e}")
    finally:
        p.window(0, 0)
        print(f"\n5) Clear BDF entries 0..{N - 1} again (the hugepages stay allocated until you confirm):\n")
        print(f"   source /nfs/amd/2025.1/Vivado/settings64.sh; sudo -E xsdb {clr_tcl}")
        while not ask("\nType 'yes' when the entries read back as all zeros: "):
            pass
        buf.free_all()
        print("hugepages released. Afterwards: sudo sysctl vm.nr_hugepages=0")


def cmd_session(p, args):
    if args.windows:
        return cmd_session_nic(p, args)
    p.check_id()
    steps = set(args.steps.split(","))
    W = args.win
    if W < 4096 or W > PAGE or W & (W - 1):
        sys.exit("--win must be a power of two from 4096 to 2 MiB")
    if p.rd64("WIN_SIZE_LO") != 0:
        print("note: probe window already set; it is reprogrammed for this session")
    p.window(0, 0)
    buf = HugeBuf(args.hugepages, ALIGN)
    pa, mm = buf.pa, buf.mm
    axi = AXI_BASE + (pa % ALIGN)              # == AXI_BASE: pa is 128 MiB aligned
    print(f"host buffer: 2 MiB hugepage at physical 0x{pa:x} (128 MiB aligned); probe AXI 0x{axi:x};"
          f" window {W} B")
    print("\n1) Program BDF entry 0 to this page (as root, in another terminal):\n")
    for line in xsdb_set(pa, args.perm, args.prot, W):
        print("   " + line)
    if not ask("\nType 'yes' when the entry is written and read back as set: "):
        print("stopped; nothing was sent to the card")
        buf.free_all()
        return
    p.window(axi, W)                           # the probe refuses anything outside it
    seed = 0x5EED_0000 | (os.getpid() & 0xFFFF)
    ok = True
    try:
        if "read" in steps:
            print(f"\n2) READ test: host fills the page with the pattern, the probe reads {W} B and compares")
            mm[0:PAGE] = pattern(axi, PAGE, seed)
            for length, beats, outst in [(64, 1, 1), (W, 64, 1), (W, 1, 32)]:
                res = p.run(2, axi, length, beats, outst, seed)
                show(res, length, f"read {length} B, {beats * 64} B bursts x{outst}: ")
                if bad(res, {"rresp_err", "mismatch", "timeout", "range_err", "param_err"}):
                    ok = False
                    print("  read test failed: no write test will run")
                    break
        if ok and "write" in steps:
            print(f"\n3) WRITE test: the probe writes 64 B, then {W} B, at page offset 0;"
                  " the rest of the page must stay 0xCC")
            if ask("   Type 'yes' to let the card write host memory: "):
                for length, s2 in [(64, seed ^ 0xFFFF), (W, seed ^ 0xFF00)]:
                    mm[0:PAGE] = b"\xCC" * PAGE
                    res = p.run(1, axi, length, min(64, length // 64), 1, s2)
                    show(res, length, f"write {length} B: ")
                    data_ok = mm[0:length] == pattern(axi, length, s2)
                    guard_ok = mm[length:PAGE] == b"\xCC" * (PAGE - length)
                    print(f"  host check: data {'OK' if data_ok else 'WRONG'}, guard {'OK' if guard_ok else 'CHANGED'}")
                    if not data_ok or not guard_ok or bad(res, {"bresp_err", "timeout", "range_err", "param_err"}):
                        ok = False
                        print("  write test failed: stopping")
                        break
            else:
                print("   write test skipped")
                steps.discard("write")
        if ok and "bw" in steps:
            modes = [("read", 2)] + ([("write", 1), ("both", 7)] if "write" in steps else [])
            mm[0:PAGE] = pattern(axi, PAGE, seed)
            print(f"\n4) {W} B runs, {args.bw_reps} each (Gb/s, card side). A run this short is"
                  " latency-bound: these are not steady-state bandwidths")
            print(f"   {'mode':6} {'burst':>6} {'outst':>5}   {'min':>7} {'avg':>7} {'max':>7}")
            for mode, cmd in modes:
                for beats in (1, 4, 16, 64):
                    if beats * 64 > W:
                        continue
                    for outst in (1, 8, 32):
                        rates = []
                        for _ in range(args.bw_reps):
                            res = p.run(cmd, axi, W, beats, outst, seed)
                            if bad(res, {"bresp_err", "rresp_err", "mismatch", "timeout"}):
                                show(res, W, f"{mode} {beats}x{outst}: ")
                                raise RuntimeError("run failed")
                            cyc = max(res["w_cycles"], res["r_cycles"])
                            rates.append(gbps(W * (2 if cmd == 7 else 1), cyc))
                        print(f"   {mode:6} {beats * 64:5}B {outst:5}   {min(rates):7.2f}"
                              f" {sum(rates) / len(rates):7.2f} {max(rates):7.2f}")
            res = p.run(2, axi, W, 1, 1, seed)
            mn, mx, sm = res["rd_lat"]
            print(f"   read latency (64 B, 1 outstanding): min {mn / SERVICE_HZ * 1e9:.0f} ns,"
                  f" avg {sm / max(1, res['r_beats']) / SERVICE_HZ * 1e9:.0f} ns, max {mx / SERVICE_HZ * 1e9:.0f} ns")
            if "write" in steps:
                res = p.run(1, axi, W, 1, 1, seed)
                mn, mx, sm = res["wr_lat"]
                print(f"   write latency AW->B (64 B, 1 outstanding): min {mn / SERVICE_HZ * 1e9:.0f} ns,"
                      f" avg {sm / max(1, res['w_bursts']) / SERVICE_HZ * 1e9:.0f} ns, max {mx / SERVICE_HZ * 1e9:.0f} ns")
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nstopped: {e}")
    finally:
        p.window(0, 0)
        print("\n5) Clear BDF entry 0 again (the hugepage stays allocated until you confirm):\n")
        for line in xsdb_clear():
            print("   " + line)
        while not ask("\nType 'yes' when entry 0 reads back as all zeros: "):
            pass
        buf.free_all()
        print("hugepage released. Afterwards: sudo sysctl vm.nr_hugepages=0")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", required=True, help="BDF of the V80, e.g. 0000:1b:00")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    s = sub.add_parser("session")
    s.add_argument("--steps", default="read,write,bw", help="comma list of read, write, bw")
    s.add_argument("--windows", type=int, default=64,
                   help="NIC static: BDF entries / 2 MiB windows to map (1-64, default 64);"
                        " 0 = m05 static (one window, --win)")
    s.add_argument("--hugepages", type=int, default=256,
                   help="hugepages to allocate (m05: scanned for a 128 MiB aligned one)")
    s.add_argument("--perm", type=lambda x: int(x, 0), default=3, help="BDF access_perm (default 3)")
    s.add_argument("--prot", type=lambda x: int(x, 0), default=2, help="BDF prot (default 2, matches AxPROT 010)")
    s.add_argument("--win", type=lambda x: int(x, 0), default=4096,
                   help="m05 static (--windows 0): usable window in bytes (default 4096: its CPM5"
                        " slave region decodes 4 KiB; further accesses get DECERR)")
    s.add_argument("--bw-reps", type=int, default=10)
    args = ap.parse_args()
    if os.geteuid() != 0:
        sys.exit("run as root (BAR2 mapping, /proc/self/pagemap)")
    p = Probe(Bar(args.device))
    {"status": cmd_status, "session": cmd_session}[args.cmd](p, args)


if __name__ == "__main__":
    main()
