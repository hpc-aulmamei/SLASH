#!/usr/bin/env python3
"""Turn the patched SLASH 1.0.0 service shell (m05) into the NIC static shell.

    [NIC_DMA_MHZ=250] patch_shell.py <slashkit dir>      e.g. shell-nic/pylib/slashkit

NIC_DMA_MHZ sets the DMA clock (default 390). 250 is the stock SLASH rate,
for hosts below Gen5 (128 Gb/s raw per direction); 390 is the Gen5 target.

Changes, all in resources/base/service:
  scripts/top.tcl
    aved/cips   CPM_PCIE1_DMA_INTF AXI_MM_and_AXI_Stream (QDMA streaming, internal
                descriptors, no bypass); slave-bridge region 0 = 128 MB in 64 x 2 MB
                windows at AXI 0xE800_0000 (stock: 4 KB, 1 window)
    aved        dma_clk_wiz: MMCM pl2_ref_clk 250 -> NIC_DMA_MHZ (CIPS PL reference
                clocks stop at 350 MHz on this part). 433.33 failed timing in the
                CPM's soft QDMA-ST logic (-0.228 ns) and the CPM user clock min
                period (2.309 ns); 400 failed by 25 ps in the same logic. It replaces pl2_ref_clk on
                the stock net: dma1_intrfc_clk, hw_discovery's cfg_ext side and
                the pcie reset (held until the MMCM locks)
                qdma_noc_bridge (qdma_st_pipe + qdma_noc_static) on the CIPS dma1
                streaming ports, and two axis_noc cells: qnoc_nmu (h2c0, h2c1,
                evt into the NoC) and qnoc_nsu (c2h0, c2h1, cmpt, cmd out of it).
                Their INIs leave aved as hierarchy pins; dma_clk / dma_resetn out
    static_region, top   the INIs and the clock passed to the service_layer container
  scripts/service_layer.tcl   the default service layer: matching INI ports, NoC
                endpoints and qdma_noc_rm_stub (drains and idles the streams)
  scripts/create_project.tcl  adds the rtl files before the block designs
  scripts/build_project.tcl   impl_1: post-route phys_opt_design -directive AggressiveExplore
  constraints/impl.xdc        pblock_service_layer gets NMU512 sites X1Y4-X3Y4, X2Y9-X2Y10 (22 -> 27)
  rtl/                        copied from this repo

Refuses to run on a tree it has already patched or that does not match.
"""
import os
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "# NIC static (slash-roce shell/patch_shell.py)"  # kept: run_nic_shell.sh and patched trees match it

DMA_HZ = int(float(os.environ.get("NIC_DMA_MHZ", "390")) * 1e6)  # dma_clk_wiz output

# CIPS dma1 streaming interface -> qdma_noc_bridge interface (CPM side)
CPM = [
    ("dma1_m_axis_h2c",      "cpm_h2c"),
    ("dma1_s_axis_c2h",      "cpm_c2h"),
    ("dma1_s_axis_c2h_cmpt", "cpm_cmpt"),
    ("dma1_axis_c2h_status", "cpm_c2h_sts"),
    ("dma1_usr_irq",         "cpm_irq"),
    ("dma1_tm_dsc_sts",      "cpm_dsc_sts"),
    ("dma1_qsts_out",        "cpm_qsts"),
    ("dma1_dsc_crdt_in",     "cpm_crdt"),
]
# NoC streams: (name, bridge/stub port suffix, TDATA bytes, reserved write MB/s).
# A 512-bit AXIS NoC port carries at most 16000 MB/s; 390 MHz x 64 B = 24960 MB/s
# per direction, so each data direction uses two ports.
NOC_TO_RM = [("QNOC_H2C0", "h2c0", 64, 12500), ("QNOC_H2C1", "h2c1", 64, 12500),
             ("QNOC_EVT", "evt", 32, 4000)]
NOC_FROM_RM = [("QNOC_C2H0", "c2h0", 64, 12500), ("QNOC_C2H1", "c2h1", 64, 12500),
               ("QNOC_CMPT", "cmpt", 64, 8000), ("QNOC_CMD", "cmd", 32, 1000)]
INIS = "xilinx.com:interface:inis_rtl:1.0"
RTL = ["qnoc_lib.v", "qnoc_crc512.v", "qnoc_ecc57.v", "qdma_st_pipe.v", "qdma_noc_static.v",
       "qdma_noc_bridge.v", "qdma_noc_rm_stub.v"]


def die(msg):
    sys.exit(f"patch_shell: {msg}")


def proc_span(text, name):
    """[start, end) of 'proc <name> ' up to the next top-level proc."""
    m = re.search(r"^proc %s \{" % re.escape(name), text, re.M)
    if not m:
        die(f"proc {name} not found")
    n = re.search(r"^proc ", text[m.end():], re.M)
    return m.start(), (m.end() + n.start()) if n else len(text)


def sub_in(text, span, old, new, count=1):
    a, b = span
    body = text[a:b]
    if body.count(old) != count:
        die(f"expected {count}x {old.strip()[:70]!r}, found {body.count(old)}")
    return text[:a] + body.replace(old, new) + text[b:]


def insert_before_restore(text, proc, block):
    a, b = proc_span(text, proc)
    anchor = "  # Restore current instance\n"
    i = text.rfind(anchor, a, b)
    if i < 0:
        die(f"no restore anchor in {proc}")
    return text[:i] + block + "\n" + text[i:]


def hier_pins():
    """INI and clock pins of the aved / static_region hierarchies (static side)"""
    lines = [f"  {MARK}"]
    for name, *_ in NOC_TO_RM:
        lines.append(f"  create_bd_intf_pin -mode Master -vlnv {INIS} M_{name}")
    for name, *_ in NOC_FROM_RM:
        lines.append(f"  create_bd_intf_pin -mode Slave -vlnv {INIS} S_{name}")
    lines.append("  create_bd_pin -dir O -type clk dma_clk")
    lines.append("  create_bd_pin -dir O -type rst dma_resetn")
    return "\n".join(lines) + "\n"


def axis_noc(cell, streams, to_noc, prefix="  "):
    """create one axis_noc cell: to_noc -> NMU (S0i_AXIS -> M0i_INIS),
    else NSU (S0i_INIS -> M0i_AXIS)"""
    n = len(streams)
    L = [f"{prefix}set {cell} [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 {cell} ]"]
    if to_noc:
        L.append(f"{prefix}set_property -dict [list CONFIG.NUM_SI {{{n}}} CONFIG.NUM_MI {{0}} CONFIG.NUM_NMI {{{n}}}] ${cell}")
    else:
        L.append(f"{prefix}set_property -dict [list CONFIG.NUM_SI {{0}} CONFIG.NUM_MI {{{n}}} CONFIG.NUM_NSI {{{n}}}] ${cell}")
    bus = []
    for i, (name, port, nbytes, bw) in enumerate(streams):
        conn = f"write_bw {{{bw}}} write_avg_burst {{8}}"
        if to_noc:
            L.append(f"{prefix}set_property -dict [list CONFIG.TDATA_NUM_BYTES {{{nbytes}}} CONFIG.TDEST_WIDTH {{0}} "
                     f"CONFIG.TID_WIDTH {{0}} CONFIG.CATEGORY {{pl}} CONFIG.CONNECTIONS {{M{i:02d}_INIS {{ {conn} }}}}] "
                     f"[get_bd_intf_pins {cell}/S{i:02d}_AXIS]")
            L.append(f"{prefix}set_property -dict [list CONFIG.INI_STRATEGY {{driver}}] [get_bd_intf_pins {cell}/M{i:02d}_INIS]")
            bus.append(f"S{i:02d}_AXIS")
        else:
            L.append(f"{prefix}set_property -dict [list CONFIG.TDATA_NUM_BYTES {{{nbytes}}} CONFIG.TDEST_WIDTH {{0}} "
                     f"CONFIG.TID_WIDTH {{0}} CONFIG.CATEGORY {{pl}}] [get_bd_intf_pins {cell}/M{i:02d}_AXIS]")
            L.append(f"{prefix}set_property -dict [list CONFIG.INI_STRATEGY {{load}} CONFIG.CONNECTIONS {{M{i:02d}_AXIS {{ {conn} }}}}] "
                     f"[get_bd_intf_pins {cell}/S{i:02d}_INIS]")
            bus.append(f"M{i:02d}_AXIS")
    L.append(f"{prefix}set_property -dict [list CONFIG.ASSOCIATED_BUSIF {{{':'.join(bus)}}}] [get_bd_pins {cell}/aclk0]")
    return L


def noc_pass(cell, streams, prefix="  "):
    """axis_noc INI pass-through: S0i_INIS (driver) -> M0i_INIS (load)"""
    n = len(streams)
    L = [f"{prefix}set {cell} [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 {cell} ]",
         f"{prefix}set_property -dict [list CONFIG.NUM_SI {{0}} CONFIG.NUM_MI {{0}} CONFIG.NUM_NSI {{{n}}} CONFIG.NUM_NMI {{{n}}}] ${cell}"]
    for i, (name, port, nbytes, bw) in enumerate(streams):
        L.append(f"{prefix}set_property -dict [list CONFIG.INI_STRATEGY {{load}}] [get_bd_intf_pins {cell}/M{i:02d}_INIS]")
        L.append(f"{prefix}set_property -dict [list CONFIG.INI_STRATEGY {{driver}} CONFIG.DEST_IDS {{}} "
                 f"CONFIG.CONNECTIONS {{M{i:02d}_INIS {{ write_bw {{{bw}}} }}}}] [get_bd_intf_pins {cell}/S{i:02d}_INIS]")
    return L


def patch_top(t):
    if MARK in t:
        die("top.tcl is already patched")

    # ---- aved: CIPS settings ----
    span = proc_span(t, "create_hier_cell_aved")
    t = sub_in(t, span, "      CPM_PCIE1_CFG_VEND_ID {10ee} \\\n",
               "      CPM_PCIE1_CFG_VEND_ID {10ee} \\\n"
               "      CPM_PCIE1_DMA_INTF {AXI_MM_and_AXI_Stream} \\\n"
               "      CPM_PCIE1_PCIE_REGION_0_EN {1} \\\n"
               "      CPM_PCIE1_PCIE_REGION_0_NUM_WINDOW {64} \\\n"
               "      CPM_PCIE1_PCIE_REGION_0_SCALE {Megabytes} \\\n"
               "      CPM_PCIE1_PCIE_REGION_0_SIZE {128} \\\n")
    if re.search(r"CPM_PCIE1_DMA_INTF \{(?!AXI_MM_and_AXI_Stream)", t[slice(*proc_span(t, "create_hier_cell_aved"))]):
        die("top.tcl sets CPM_PCIE1_DMA_INTF already")
    # ---- aved: DMA clock MMCM on the stock pl2 net ----
    span = proc_span(t, "create_hier_cell_aved")
    t = sub_in(t, span, "  # Create interface connections\n",
               f"  {MARK}\n"
               "  # DMA interface clock: " + f"{DMA_HZ / 1e6:g}" + " MHz (PG347 Gen5x8 range on -2MHP: 366-433)\n"
               "  set dma_clk_wiz [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wizard:1.0 dma_clk_wiz ]\n"
               "  set_property -dict [list \\\n"
               "    CONFIG.CLKOUT_DRIVES {BUFG} \\\n"
               f"    CONFIG.CLKOUT_REQUESTED_OUT_FREQUENCY {{{DMA_HZ / 1e6:.3f}}} \\\n"
               "    CONFIG.CLKOUT_USED {true} \\\n"
               "    CONFIG.PRIM_SOURCE {No_buffer} \\\n"
               "    CONFIG.USE_DYN_RECONFIG {false} \\\n"
               "    CONFIG.USE_LOCKED {true} \\\n"
               "    CONFIG.USE_POWER_DOWN {false} \\\n"
               "    CONFIG.USE_RESET {false} \\\n"
               "  ] $dma_clk_wiz\n\n"
               "  # Create interface connections\n")
    span = proc_span(t, "create_hier_cell_aved")
    t = sub_in(t, span,
               "  connect_bd_net -net cips_pl2_ref_clk  [get_bd_pins cips/pl2_ref_clk] \\\n"
               "  [get_bd_pins cips/dma1_intrfc_clk] \\\n",
               "  connect_bd_net -net cips_pl2_ref_clk  [get_bd_pins cips/pl2_ref_clk] \\\n"
               "  [get_bd_pins dma_clk_wiz/clk_in1]\n"
               "  connect_bd_net -net dma_clk_wiz_clk_out1  [get_bd_pins dma_clk_wiz/clk_out1] \\\n"
               "  [get_bd_pins cips/dma1_intrfc_clk] \\\n")

    # ---- clock_reset: pcie reset waits for the MMCM ----
    span = proc_span(t, "create_hier_cell_clock_reset")
    t = sub_in(t, span, "  create_bd_pin -dir I -type clk clk_pcie\n",
               "  create_bd_pin -dir I -type clk clk_pcie\n"
               f"  {MARK}\n"
               "  create_bd_pin -dir I clk_pcie_locked\n")
    t = insert_before_restore(t, "create_hier_cell_clock_reset",
                              f"  {MARK}\n"
                              "  connect_bd_net [get_bd_pins clk_pcie_locked] [get_bd_pins pcie_psr/dcm_locked]\n")

    # ---- aved: pins, pipeline, connections ----
    span = proc_span(t, "create_hier_cell_aved")
    t = sub_in(t, span, "  create_bd_pin -dir O -type rst pl0_resetn\n",
               "  create_bd_pin -dir O -type rst pl0_resetn\n" + hier_pins())
    blk = [f"  {MARK}",
           "  # QDMA streaming: CIPS dma1 ports -> qdma_noc_bridge -> NoC -> service layer",
           "  set qdma_noc [ create_bd_cell -type module -reference qdma_noc_bridge qdma_noc ]",
           "  set_property CONFIG.STAGES {2} $qdma_noc"]
    for cips_if, cpm_if in CPM:
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins cips/{cips_if}] [get_bd_intf_pins qdma_noc/{cpm_if}]")
    blk += axis_noc("qnoc_nmu", NOC_TO_RM, True)
    blk += axis_noc("qnoc_nsu", NOC_FROM_RM, False)
    for i, (name, port, *_) in enumerate(NOC_TO_RM):
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins qdma_noc/m_{port}] [get_bd_intf_pins qnoc_nmu/S{i:02d}_AXIS]")
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_nmu/M{i:02d}_INIS] [get_bd_intf_pins M_{name}]")
    for i, (name, port, *_) in enumerate(NOC_FROM_RM):
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins S_{name}] [get_bd_intf_pins qnoc_nsu/S{i:02d}_INIS]")
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_nsu/M{i:02d}_AXIS] [get_bd_intf_pins qdma_noc/s_{port}]")
    blk += ["  connect_bd_net [get_bd_pins dma_clk_wiz/clk_out1] [get_bd_pins qdma_noc/clk] [get_bd_pins qnoc_nmu/aclk0] \\",
            "    [get_bd_pins qnoc_nsu/aclk0] [get_bd_pins dma_clk]",
            "  connect_bd_net [get_bd_pins dma_clk_wiz/locked] [get_bd_pins clock_reset/clk_pcie_locked]",
            "  connect_bd_net [get_bd_pins cips/dma1_axi_aresetn] [get_bd_pins qdma_noc/rstn] [get_bd_pins dma_resetn]"]
    t = insert_before_restore(t, "create_hier_cell_aved", "\n".join(blk) + "\n")

    # ---- static_region: pass through ----
    span = proc_span(t, "create_hier_cell_static_region")
    t = sub_in(t, span, "  create_bd_pin -dir O -from 0 -to 0 -type rst peripheral_aresetn2\n",
               "  create_bd_pin -dir O -from 0 -to 0 -type rst peripheral_aresetn2\n" + hier_pins())
    # The INI strategy must change from driver to load inside a NoC instance
    # (BD 41-2199), so every INI passes an axis_noc pass-through here, the way
    # SLASH's static_region/dcmac_noc does for the DCMAC streams.
    blk = [f"  {MARK}"]
    blk += noc_pass("qnoc_pass_out", NOC_TO_RM)
    blk += noc_pass("qnoc_pass_in", NOC_FROM_RM)
    for i, (name, *_) in enumerate(NOC_TO_RM):
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins aved/M_{name}] [get_bd_intf_pins qnoc_pass_out/S{i:02d}_INIS]")
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_pass_out/M{i:02d}_INIS] [get_bd_intf_pins M_{name}]")
    for i, (name, *_) in enumerate(NOC_FROM_RM):
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins S_{name}] [get_bd_intf_pins qnoc_pass_in/S{i:02d}_INIS]")
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_pass_in/M{i:02d}_INIS] [get_bd_intf_pins aved/S_{name}]")
    blk += ["  connect_bd_net [get_bd_pins aved/dma_clk] [get_bd_pins dma_clk]",
            "  connect_bd_net [get_bd_pins aved/dma_resetn] [get_bd_pins dma_resetn]"]
    t = insert_before_restore(t, "create_hier_cell_static_region", "\n".join(blk) + "\n")

    # ---- root: static_region <-> service_layer container ----
    anchor = "  set_property APERTURES {{0xE000_0000 256M}} [get_bd_intf_pins /service_layer/M_QDMA_SLV_BRIDGE]\n"
    if t.count(anchor) != 1:
        die("service_layer container anchor not found once")
    blk = [f"  {MARK}"]
    for name, *_ in NOC_TO_RM:
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins static_region/M_{name}] [get_bd_intf_pins service_layer/S_{name}]")
    for name, *_ in NOC_FROM_RM:
        blk.append(f"  connect_bd_intf_net [get_bd_intf_pins service_layer/M_{name}] [get_bd_intf_pins static_region/S_{name}]")
    blk += ["  connect_bd_net [get_bd_pins static_region/dma_clk] [get_bd_pins service_layer/dma_clk]",
            "  connect_bd_net [get_bd_pins static_region/dma_resetn] [get_bd_pins service_layer/dma_resetn]"]
    t = t.replace(anchor, anchor + "\n".join(blk) + "\n")
    return t


def rm_ports():
    """default service layer: INI ports, NoC endpoints and the stub"""
    L = [f"  {MARK}"]
    for name, *_ in NOC_TO_RM:
        L.append(f"  set S_{name} [ create_bd_intf_port -mode Slave -vlnv {INIS} S_{name} ]")
        L.append(f"  set_property -dict [list CONFIG.COMPUTED_STRATEGY {{load}} CONFIG.INI_STRATEGY {{load}}] $S_{name}")
    for name, *_ in NOC_FROM_RM:
        L.append(f"  set M_{name} [ create_bd_intf_port -mode Master -vlnv {INIS} M_{name} ]")
        L.append(f"  set_property -dict [list CONFIG.COMPUTED_STRATEGY {{driver}} CONFIG.INI_STRATEGY {{driver}}] $M_{name}")
    L += [f"  set dma_clk [ create_bd_port -dir I -type clk -freq_hz {DMA_HZ} dma_clk ]",
          "  set_property CONFIG.ASSOCIATED_RESET {dma_resetn} $dma_clk",
          "  create_bd_port -dir I -type rst dma_resetn",
          "  # QDMA NoC streams end here; replace qnoc_stub with qdma_noc_rm and logic",
          "  set qnoc_stub [ create_bd_cell -type module -reference qdma_noc_rm_stub qnoc_stub ]"]
    L += axis_noc("qnoc_rm_nsu", NOC_TO_RM, False)
    L += axis_noc("qnoc_rm_nmu", NOC_FROM_RM, True)
    for i, (name, port, *_) in enumerate(NOC_TO_RM):
        L.append(f"  connect_bd_intf_net [get_bd_intf_ports S_{name}] [get_bd_intf_pins qnoc_rm_nsu/S{i:02d}_INIS]")
        L.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_rm_nsu/M{i:02d}_AXIS] [get_bd_intf_pins qnoc_stub/s_{port}]")
    for i, (name, port, *_) in enumerate(NOC_FROM_RM):
        L.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_stub/m_{port}] [get_bd_intf_pins qnoc_rm_nmu/S{i:02d}_AXIS]")
        L.append(f"  connect_bd_intf_net [get_bd_intf_pins qnoc_rm_nmu/M{i:02d}_INIS] [get_bd_intf_ports M_{name}]")
    L.append("  connect_bd_net [get_bd_ports dma_clk] [get_bd_pins qnoc_stub/clk] [get_bd_pins qnoc_rm_nsu/aclk0] [get_bd_pins qnoc_rm_nmu/aclk0]")
    return "\n".join(L) + "\n"


def patch_service_layer(t):
    if MARK in t:
        die("service_layer.tcl is already patched")
    anchor = "  set arstn [ create_bd_port -dir I -type rst arstn ]\n"
    if t.count(anchor) != 1:
        die("service_layer.tcl arstn anchor not found once")
    return t.replace(anchor, anchor + rm_ports())


def patch_create_project(t):
    if MARK in t:
        die("create_project.tcl is already patched")
    anchor = '  source [file normalize [file join $src_dir "slash_base.tcl"]]\n'
    if t.count(anchor) != 1:
        die("create_project.tcl anchor not found once")
    add = f"  {MARK}\n"
    for f in RTL:
        add += f'  add_files -norecurse [file normalize [file join $src_dir ".." "rtl" "{f}"]]\n'
    add += "  update_compile_order -fileset sources_1\n"
    return t.replace(anchor, add + anchor)


def patch_build_project(t):
    if MARK in t:
        die("build_project.tcl is already patched")
    anchor = "  set_property STEPS.WRITE_DEVICE_IMAGE.TCL.PRE  [get_files *write_device_image.pre.tcl]  [get_runs impl_1]\n"
    if t.count(anchor) != 1:
        die("build_project.tcl anchor not found once")
    add = (f"  {MARK}\n"
           "  set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]\n"
           "  set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]\n")
    return t.replace(anchor, anchor + add)


# The stock service pblock has 22 NMU512 sites and its default RM already uses 21
# (8 DCMAC INIS, 8 sl2noc, 4 noc_virt, the slave bridge); the four QNOC streams
# out of the RM make 25. Row Y4 of NoC columns X1-X3 sits in clock-region row 3
# next to fabric the pblock already owns (SLICE_X48Y236 and up). With only those
# (25 sites for 25 NMUs) the placer could not seat four sl2noc NMUs "based on bin
# tile": the NMU must be near where its driver lands, and Y4 is at the bottom.
# X2Y9/X2Y10 (in the static pocket SLICE_X164-243 Y428-524, middle of the region)
# give the slack that made NoC placement pass on the same netlist. X0Y4 borders
# static fabric only. (HDPR-134 "disjoint pblock" is reported for the stock
# pblock too.)
NMU_EXTRA = "NOC_NMU512_X1Y4:NOC_NMU512_X3Y4 NOC_NMU512_X2Y9:NOC_NMU512_X2Y10"


def patch_impl_xdc(t):
    if MARK in t:
        die("impl.xdc is already patched")
    anchor = ("resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NMU512_X3Y5:NOC_NMU512_X3Y10 "
              "NOC_NMU512_X2Y5:NOC_NMU512_X2Y8 NOC_NMU512_X0Y5:NOC_NMU512_X1Y10}\n")
    if t.count(anchor) != 1:
        die("impl.xdc service-layer NMU line not found once")
    add = (f"{MARK}\n"
           f"resize_pblock [get_pblocks pblock_service_layer] -add {{{NMU_EXTRA}}}\n")
    return t.replace(anchor, anchor + add)


def main():
    if len(sys.argv) != 2:
        die(__doc__)
    sk = Path(sys.argv[1]).resolve()
    base = sk / "resources" / "base" / "service"
    scripts = base / "scripts"
    plm = sk / "resources" / "aved" / "plm" / "build-plm.sh"
    if not (scripts / "top.tcl").is_file():
        die(f"{scripts}/top.tcl not found")
    if plm.is_file() and '"versal_sdt" not in path.parts' not in plm.read_text():
        die(f"{plm} lacks the m05 versal_sdt patch")

    new = {
        scripts / "top.tcl": patch_top((scripts / "top.tcl").read_text()),
        scripts / "service_layer.tcl": patch_service_layer((scripts / "service_layer.tcl").read_text()),
        scripts / "create_project.tcl": patch_create_project((scripts / "create_project.tcl").read_text()),
        scripts / "build_project.tcl": patch_build_project((scripts / "build_project.tcl").read_text()),
        base / "constraints" / "impl.xdc": patch_impl_xdc((base / "constraints" / "impl.xdc").read_text()),
    }
    for p, s in new.items():
        p.write_text(s)
        print(f"patched {p}")
    (base / "rtl").mkdir(exist_ok=True)
    for f in RTL:
        shutil.copy2(HERE / "rtl" / f, base / "rtl" / f)
        print(f"copied  {base / 'rtl' / f}")


if __name__ == "__main__":
    main()
