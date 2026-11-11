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

"""An R5 path to the CPM AXI slave, on top of the NIC static.

    patch_rpuq.py <slashkit dir>      (tree already patched by nic/shell/patch_shell.py)

R5 M_AXI_LPD -> base_logic/rpu_sc/M03_AXI -> base_logic/axi_noc_rpuq (PL NMU, remaps)
  -> INI -> static_region/axi_noc_1/S01_INI -> M00_AXI -> cips/NOC_CPM_PCIE_0

  R5 0x9000_0000 128M -> 0xE800_0000      slave bridge region 0: the 64 x 2 MB BDF
                                          windows (host memory), psv_noc_pcie_0
  R5 0x9800_0000  16M -> 0x7_1000_0000    CPM5 DMA1 "MULTQ" decode
                                          (CPM_AXI_SLV_MULTQ1_BASE_ADDR): QDMA CSRs
                                          and queue space for all functions, psv_noc_pcie_1

PG347 "Slave Bridge Registers Limitations": register accesses through the AXI
slave must have AxCACHE[1] = 0 and be 4 bytes; from the RPU use Device memory
(nGnRE / nGnRnE) for the whole 0x9000_0000-0x9FFF_FFFF range.

The service layer's own route (axi4_full_passthrough_4 -> 0xE000_0000 256M via
axi_noc_1/S00_INI) is unchanged. No mailbox IP: the AXI path reaches every
function's CSRs, and dma1_usr_irq stays with the RM.
"""
import re
import sys
from pathlib import Path

MARK = "# RPU QDMA mastering (nic/shell/patch_rpuq.py)"
NIC_MARK = "# NIC static (slash-roce shell/patch_shell.py)"

R5_HOST, HOST_PHYS, HOST_SIZE = 0x9000_0000, 0xE800_0000, 0x0800_0000
R5_QDMA, QDMA_PHYS, QDMA_SIZE = 0x9800_0000, 0x7_1000_0000, 0x0100_0000


def die(msg):
    sys.exit(f"patch_rpuq: {msg}")


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


def insert_before_restore(text, proc, block):
    a, b = proc_span(text, proc)
    anchor = "  # Restore current instance\n"
    i = text.rfind(anchor, a, b)
    if i < 0:
        die(f"no restore anchor in {proc}")
    return text[:i] + block + "\n" + text[i:]


def tclhex(v):
    """0x7_1000_0000 style, as the stock REMAPS use"""
    h = f"{v:x}"
    groups = []
    while h:
        groups.insert(0, h[-4:])
        h = h[:-4]
    return "0x" + "_".join(groups)


def patch_top(t):
    if MARK in t:
        die("top.tcl is already patched")
    if NIC_MARK not in t:
        die("top.tcl lacks the NIC static patch (run nic/shell/patch_shell.py first)")

    # ---- base_logic: rpu_sc M03_AXI -> new NMU -> M01_INI ----
    p = "create_hier_cell_base_logic"
    t = sub_in(t, p,
               "  set rpu_sc [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 rpu_sc ]\n"
               "  set_property -dict [list \\\n"
               "    CONFIG.NUM_CLKS {1} \\\n"
               "    CONFIG.NUM_MI {3} \\\n",
               "  set rpu_sc [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 rpu_sc ]\n"
               "  set_property -dict [list \\\n"
               "    CONFIG.NUM_CLKS {1} \\\n"
               f"    CONFIG.NUM_MI {{4}} \\\n")
    t = sub_in(t, p,
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI\n",
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI\n"
               f"  {MARK}\n"
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M01_INI\n")
    remaps = (f"{{{tclhex(R5_HOST)} {tclhex(HOST_PHYS)} 0x{HOST_SIZE:x}}} "
              f"{{{tclhex(R5_QDMA)} {tclhex(QDMA_PHYS)} 0x{QDMA_SIZE:x}}}")
    blk = "\n".join([
        f"  {MARK}",
        "  # R5 -> CPM AXI slave (NOC_CPM_PCIE_0): slave-bridge windows and QDMA registers",
        "  set axi_noc_rpuq [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 axi_noc_rpuq ]",
        "  set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {0} CONFIG.NUM_NMI {1}] $axi_noc_rpuq",
        "  set_property -dict [ list \\",
        "   CONFIG.CONNECTIONS {M00_INI {read_bw {100} write_bw {100}}} \\",
        "   CONFIG.DEST_IDS {} \\",
        f"   CONFIG.REMAPS {{M00_INI {{{remaps}}}}} \\",
        "   CONFIG.NOC_PARAMS {} \\",
        "   CONFIG.CATEGORY {pl} \\",
        " ] [get_bd_intf_pins $axi_noc_rpuq/S00_AXI]",
        "  set_property CONFIG.ASSOCIATED_BUSIF {S00_AXI} [get_bd_pins $axi_noc_rpuq/aclk0]",
        "  connect_bd_intf_net [get_bd_intf_pins rpu_sc/M03_AXI] [get_bd_intf_pins axi_noc_rpuq/S00_AXI]",
        "  connect_bd_intf_net [get_bd_intf_pins axi_noc_rpuq/M00_INI] [get_bd_intf_pins M01_INI]",
        "  connect_bd_net [get_bd_pins clk_pl] [get_bd_pins axi_noc_rpuq/aclk0]",
    ]) + "\n"
    t = insert_before_restore(t, p, blk)

    # ---- aved: pass M01_INI out ----
    p = "create_hier_cell_aved"
    t = sub_in(t, p,
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI\n",
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI\n"
               f"  {MARK}\n"
               "  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M01_INI\n")
    t = insert_before_restore(t, p, f"  {MARK}\n"
                              "  connect_bd_intf_net [get_bd_intf_pins base_logic/M01_INI] [get_bd_intf_pins M01_INI]\n")

    # ---- static_region: second INI into the CPM NSU ----
    p = "create_hier_cell_static_region"
    t = sub_in(t, p,
               "  set axi_noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 axi_noc_1 ]\n"
               "  set_property -dict [list \\\n"
               "    CONFIG.NUM_CLKS {2} \\\n"
               "    CONFIG.NUM_NSI {1} \\\n",
               "  set axi_noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 axi_noc_1 ]\n"
               "  set_property -dict [list \\\n"
               "    CONFIG.NUM_CLKS {2} \\\n"
               "    CONFIG.NUM_NSI {2} \\\n")
    t = insert_before_restore(t, p, "\n".join([
        f"  {MARK}",
        "  set_property -dict [ list \\",
        "   CONFIG.CONNECTIONS {M00_AXI {read_bw {100} write_bw {100} read_avg_burst {4} write_avg_burst {4}}} \\",
        " ] [get_bd_intf_pins /static_region/axi_noc_1/S01_INI]",
        "  connect_bd_intf_net [get_bd_intf_pins aved/M01_INI] [get_bd_intf_pins axi_noc_1/S01_INI]",
    ]) + "\n")

    # ---- root address map: M_AXI_LPD sees the two windows, never psv_noc_pcie_2 ----
    anchor = "  validate_bd_design\n  save_bd_design\n}\n# End of create_root_design()\n"
    if t.count(anchor) != 1:
        die("create_root_design end anchor not found once")
    lpd = "[get_bd_addr_spaces static_region/aved/cips/M_AXI_LPD]"
    seg = "[get_bd_addr_segs static_region/aved/cips/NOC_CPM_PCIE_0/pspmc_0_psv_noc_pcie_{}]"
    addr = "\n".join([
        f"  {MARK}",
        f"  assign_bd_address -offset 0x{R5_HOST:08X} -range 0x{HOST_SIZE:08X} -target_address_space {lpd} {seg.format(0)} -force",
        f"  assign_bd_address -offset 0x{R5_QDMA:08X} -range 0x{QDMA_SIZE:08X} -target_address_space {lpd} {seg.format(1)} -force",
        f"  catch {{exclude_bd_addr_seg -target_address_space {lpd} {seg.format(2)}}}",
        "  foreach s [get_bd_addr_segs -of_objects " + lpd + " -filter {NAME =~ *noc_pcie*}] {",
        "    puts \"RPUQ:   $s offset [get_property OFFSET $s] range [get_property RANGE $s] excluded [get_property EXCLUDED $s]\"",
        "  }",
    ]) + "\n\n"
    t = t.replace(anchor, addr + anchor)
    return t


def main():
    if len(sys.argv) != 2:
        die(__doc__)
    top = Path(sys.argv[1]).resolve() / "resources" / "base" / "service" / "scripts" / "top.tcl"
    if not top.is_file():
        die(f"{top} not found")
    top.write_text(patch_top(top.read_text()))
    print(f"patched {top}")

    # NoC site locks (bdf static's solution + the new NMU); see rpuq_noc_locs.xdc
    xdc = top.parents[1] / "constraints" / "impl.xdc"
    x = xdc.read_text()
    if MARK + ": NoC site locks" in x:
        die(f"{xdc} already has the NoC site locks")
    locs = (Path(__file__).resolve().parent / "rpuq_noc_locs.xdc").read_text()
    xdc.write_text(x.rstrip("\n") + "\n" + locs)
    print(f"patched {xdc}")


if __name__ == "__main__":
    main()
