#!/usr/bin/env python3
"""Smart-NIC additions to the NIC static, on top of the rpuq static.

    patch_smart.py <slashkit dir>      (tree patched by patch_shell.py, then patch_rpuq.py)

1. The service-layer RM reaches the QDMA queue space.
   M_QDMA_SLV_BRIDGE (noc_virt_4 -> static axi_noc_1/S00_INI -> NOC_CPM_PCIE_0)
   gets a second aperture, 0x7_1000_0000 16M: the CPM5 DMA1 "MULTQ" decode
   (QDMA CSRs and doorbells of every function), the same window the R5 reaches
   at 0x9800_0000 on the rpuq static. An RM cannot add apertures itself
   (ModularNoC 90-3), so this lives in the static. RM logic can then ring
   doorbells (H2C PIDX at fn*0x10000 + 0x18004 + 0x10*qidx, as proven from the
   R5) and, with descriptors written to host rings through the slave bridge,
   drive QDMA queues without descriptor bypass. PG347: register accesses
   through the AXI slave must be 4 bytes with AxCACHE[1] = 0.
   The default service layer (placeholder) does not use the new aperture; the
   RM template (templates/service_layer.tcl) declares it for RM builds.

2. The R5 reaches the service-layer RM's AXI-Lite registers.
   R5 M_AXI_LPD -> rpu_sc/M02_AXI -> base_logic/axi_noc_0 (the existing user
   region NMU) gets a second remap, R5 0x9C00_0000 64M -> 0x203_0000_0000, and
   axi_noc_cips/S24_INI (its INI) a connection to M05_INI, the service layer's
   S_AXILITE_INI (aperture 0x203_0000_0000 128M). That is the lower 64 MiB of
   the PF2 BAR2 service-layer map; RM registers meant for the R5 go there.
   rpu_sc decodes only assigned segments, and an RM's segments are unknown to
   the static, so M_AXI_LPD maps the placeholder's register segments at their
   host offsets inside the window (one 64 MiB segment overlaps the
   placeholder's own SmartConnect slaves). The NoC routes the INI aperture, so
   an RM register at one of those offsets is reached: 0x203_0000_0000 +
   0x0_0000-0x7_FFFF (8 x 64 KiB), 0x9_0000-0xB_FFFF, 0xD_0000-0xF_FFFF, the
   two 256 KiB DCMAC windows 0x200_0000 and 0x300_0000, and a few 256 B ones.

No CIPS change and no new NoC master: the rpuq NoC site locks still apply.
"""
import re
import sys
from pathlib import Path

MARK = "# Smart NIC static (nic/shell/patch_smart.py)"
RPUQ_MARK = "# RPU QDMA mastering (nic/shell/patch_rpuq.py)"

QSPC_PHYS, QSPC_SIZE = 0x7_1000_0000, "16M"
R5_SL, SL_PHYS, SL_SIZE = 0x9C00_0000, 0x203_0000_0000, 0x0400_0000
SL_SEG = "service_layer/eth_0/s_axi_control/Reg"     # placeholder segment at 0x203_0000_0000


def die(msg):
    sys.exit(f"patch_smart: {msg}")


def proc_span(text, name):
    m = re.search(r"^proc %s \{" % re.escape(name), text, re.M)
    if not m:
        die(f"proc {name} not found")
    n = re.search(r"^proc ", text[m.end():], re.M)
    return m.start(), (m.end() + n.start()) if n else len(text)


def sub_in(text, proc, old, new, count=1):
    a, b = proc_span(text, proc)
    body = text[a:b]
    if body.count(old) != count:
        die(f"{proc}: expected {count}x {old.strip()[:80]!r}, found {body.count(old)}")
    return text[:a] + body.replace(old, new) + text[b:]


def sub_once(text, old, new, what):
    if text.count(old) != 1:
        die(f"{what}: expected 1x {old.strip()[:80]!r}, found {text.count(old)}")
    return text.replace(old, new)


def tclhex(v):
    h = f"{v:x}"
    groups = []
    while h:
        groups.insert(0, h[-4:])
        h = h[:-4]
    return "0x" + "_".join(groups)


QSPC_AP = f"{{{tclhex(QSPC_PHYS)} {QSPC_SIZE}}}"


def patch_top(t):
    if MARK in t:
        die("top.tcl is already patched")
    if RPUQ_MARK not in t:
        die("top.tcl lacks the rpuq patch (run patch_shell.py, then patch_rpuq.py first)")

    # 1. second aperture on the RM's CPM route
    old = "  set_property APERTURES {{0xE000_0000 256M}} [get_bd_intf_pins /service_layer/M_QDMA_SLV_BRIDGE]\n"
    t = sub_once(t, old,
                 f"  {MARK}: the RM also reaches the QDMA queue space\n"
                 f"  set_property APERTURES {{{{0xE000_0000 256M}} {QSPC_AP}}} "
                 "[get_bd_intf_pins /service_layer/M_QDMA_SLV_BRIDGE]\n",
                 "M_QDMA_SLV_BRIDGE apertures")

    # 2a. R5 user-region NMU: second remap into the service layer
    old = "   CONFIG.REMAPS {M00_INI {{0x8800_0000 0x202_0000_0000 0x8000000}}} \\\n"
    t = sub_in(t, "create_hier_cell_base_logic", old,
               "   CONFIG.REMAPS {M00_INI {{0x8800_0000 0x202_0000_0000 0x8000000} "
               f"{{{tclhex(R5_SL)} {tclhex(SL_PHYS)} 0x{SL_SIZE:x}}}}}}} \\\n")

    # 2b. its INI (axi_noc_cips/S24_INI) also to M05_INI (service layer AXI-Lite)
    old = ("   CONFIG.CONNECTIONS {M04_INI {read_bw {5} write_bw {5} initial_boot {false}}} \\\n"
           " ] [get_bd_intf_pins /static_region/noc/axi_noc_cips/S24_INI]\n")
    t = sub_once(t, old,
                 "   CONFIG.CONNECTIONS {M04_INI {read_bw {5} write_bw {5} initial_boot {false}} "
                 "M05_INI {read_bw {5} write_bw {5} initial_boot {false}}} \\\n"
                 " ] [get_bd_intf_pins /static_region/noc/axi_noc_cips/S24_INI]\n",
                 "S24_INI connections")

    # 2c. address map: M_AXI_LPD sees the service layer's lower 64 MiB as one window
    anchor = "  validate_bd_design\n  save_bd_design\n}\n# End of create_root_design()\n"
    if t.count(anchor) != 1:
        die("create_root_design end anchor not found once")
    lpd = "[get_bd_addr_spaces static_region/aved/cips/M_AXI_LPD]"
    # The placeholder's service-layer register segments, as the host (CPM_PCIE_NOC_0)
    # maps them, are mapped for the R5 at the same offsets inside the window. A
    # single 64 MiB segment would also land in the placeholder's own SmartConnect
    # and overlap its other slaves (BD 5-702), so the R5 decode (rpu_sc) is the
    # union of these segments; an RM's R5-visible registers go at these offsets,
    # e.g. 0x203_0000_0000-0x203_0007_FFFF (eth_0..eth_7, 8 x 64 KiB).
    cpm = re.findall(r"assign_bd_address -offset 0x([0-9A-Fa-f]+) -range 0x([0-9A-Fa-f]+) (?:-with_name \S+ )?"
                     r"-target_address_space \[get_bd_addr_spaces static_region/aved/cips/CPM_PCIE_NOC_0\] "
                     r"\[get_bd_addr_segs (service_layer/[^\]]+)\]", t)
    if len(cpm) < 10 or SL_SEG not in [c[2] for c in cpm]:
        die(f"service-layer segments of CPM_PCIE_NOC_0 not as expected ({len(cpm)} found)")
    lines = [f"  {MARK}: R5 window 0x{R5_SL:08X} = service layer 0x{SL_PHYS:x}"]
    for off, rng, seg in sorted(set(cpm)):
        off, rng = int(off, 16), int(rng, 16)
        if not (SL_PHYS <= off and off + rng <= SL_PHYS + SL_SIZE):
            die(f"{seg} at 0x{off:x} is outside the R5 window")
        name = "SEG_r5_" + re.sub(r"[^A-Za-z0-9]", "_", seg.removeprefix("service_layer/"))
        lines.append(f"  assign_bd_address -offset 0x{R5_SL + off - SL_PHYS:08X} -range 0x{rng:08X} -with_name {name} "
                     f"-target_address_space {lpd} [get_bd_addr_segs {seg}] -force")
    addr = "\n".join(lines + [
        "  foreach s [get_bd_addr_segs -of_objects " + lpd + "] {",
        "    set p [get_property PATH $s]",
        "    if {[string match *service_layer* $p] || [string match *noc_pcie* $p] || [string match *eth_0* $p]} {",
        "      puts \"SMART:   $p offset [get_property OFFSET $s] range [get_property RANGE $s] excluded [get_property EXCLUDED $s]\"",
        "    }",
        "  }",
        "  foreach s [get_bd_addr_segs -of_objects [get_bd_addr_spaces service_layer/axi4_full_passthrough_4/m_axi]] {",
        "    puts \"SMART:   passthrough_4 [get_property PATH $s] offset [get_property OFFSET $s] range [get_property RANGE $s] excluded [get_property EXCLUDED $s]\"",
        "  }",
        "  puts \"SMART: M_QDMA_SLV_BRIDGE apertures [get_property APERTURES [get_bd_intf_pins /service_layer/M_QDMA_SLV_BRIDGE]]\"",
    ]) + "\n\n"
    t = t.replace(anchor, addr + anchor)
    return t


def patch_rm_template(t):
    if MARK in t:
        die("templates/service_layer.tcl is already patched")
    old = "set_property APERTURES {{0xE000_0000 256M}} [get_bd_intf_ports M_QDMA_SLV_BRIDGE]\n"
    return sub_once(t, old,
                    f"{MARK}: the RM also reaches the QDMA queue space\n"
                    f"set_property APERTURES {{{{0xE000_0000 256M}} {QSPC_AP}}} [get_bd_intf_ports M_QDMA_SLV_BRIDGE]\n",
                    "RM template M_QDMA_SLV_BRIDGE apertures")


def main():
    if len(sys.argv) != 2:
        die(__doc__)
    res = Path(sys.argv[1]).resolve() / "resources"
    top = res / "base" / "service" / "scripts" / "top.tcl"
    tpl = res / "templates" / "service_layer.tcl"
    for f in (top, tpl):
        if not f.is_file():
            die(f"{f} not found")
    new_top = patch_top(top.read_text())
    new_tpl = patch_rm_template(tpl.read_text())
    top.write_text(new_top)
    print(f"patched {top}")
    tpl.write_text(new_tpl)
    print(f"patched {tpl}")


if __name__ == "__main__":
    main()
