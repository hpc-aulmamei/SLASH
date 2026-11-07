#!/usr/bin/env python3
"""Generate service_layer.tcl for the nictest RM of the NIC static shell.

The SLASH (dev) service-layer template provides the reconfigurable module's
NoC boundary (S_AXILITE_INI, the DCMAC INIS ports, SL2NOC, the virtual NoC
and QDMA bridge pass-throughs). It is rendered with no Ethernet ports, so it
creates neither DCMACs nor the control SmartConnect. The RM's contents,
tcl/nictest_service_layer.tcl, are inserted before the address assignment:
host_probe (card -> host through the CPM5 slave bridge) and qnoc_lb, a QDMA
streaming sink, loopback and generator behind qdma_noc_rm.

The NIC static (shell/patch_shell.py) gives the base block design the QDMA
stream ports (S_QNOC_*, M_QNOC_*, dma_clk, dma_resetn); this script adds the
NoC endpoints for them, built from the stream table in shell/patch_shell.py
so the two sides cannot drift apart.

usage: gen_service_layer.py <slash checkout> <out.tcl> <nic dir>
"""
import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

KIND = "nictest"
# shell/rtl files the RM needs for the QDMA streams
QNOC_RTL = ["qnoc_lib.v", "qdma_noc_rm.v"]


def qnoc_block(root):
    """tcl: QDMA stream NoC endpoints, connected to cell qnoc_lb (created by
    nictest_service_layer.tcl; it has qdma_noc_rm_stub's interface names)"""
    sys.path.insert(0, str(root / "shell"))
    import patch_shell as ps
    target = "qnoc_lb"
    L = ["# ===== QDMA streams over the NoC (shell/patch_shell.py tables) ====="]
    files = " ".join(f"{{{root / 'shell' / 'rtl' / f}}}" for f in QNOC_RTL)
    L.append(f"add_files -fileset sources_1 -norecurse [list {files}]")
    L.append("update_compile_order -fileset sources_1")
    L += ps.axis_noc("qnoc_rm_nsu", ps.NOC_TO_RM, False, prefix="")
    L += ps.axis_noc("qnoc_rm_nmu", ps.NOC_FROM_RM, True, prefix="")
    for i, (name, port, *_) in enumerate(ps.NOC_TO_RM):
        L.append(f"connect_bd_intf_net [get_bd_intf_ports S_{name}] [get_bd_intf_pins qnoc_rm_nsu/S{i:02d}_INIS]")
        L.append(f"connect_bd_intf_net [get_bd_intf_pins qnoc_rm_nsu/M{i:02d}_AXIS] [get_bd_intf_pins {target}/s_{port}]")
    for i, (name, port, *_) in enumerate(ps.NOC_FROM_RM):
        L.append(f"connect_bd_intf_net [get_bd_intf_pins {target}/m_{port}] [get_bd_intf_pins qnoc_rm_nmu/S{i:02d}_AXIS]")
        L.append(f"connect_bd_intf_net [get_bd_intf_pins qnoc_rm_nmu/M{i:02d}_INIS] [get_bd_intf_ports M_{name}]")
    # qnoc_lb on service_clk (250 MHz): at 390 MHz its paths and the static's
    # reset missed by up to 1.6 ns; the NoC crosses to the static's DMA clock.
    L.append(f"connect_bd_net [get_bd_ports service_clk] [get_bd_pins {target}/clk] "
             "[get_bd_pins qnoc_rm_nsu/aclk0] [get_bd_pins qnoc_rm_nmu/aclk0]")
    L.append(f"connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins {target}/rstn]")
    return "\n".join(L) + "\n"


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    slash, out, root = (Path(a).resolve() for a in sys.argv[1:4])
    kind = KIND

    sys.path.insert(0, str(slash / "linker"))
    from slashkit.emit.hw.service_region.service_layer_ctx import (
        NetworkSpecView, build_service_layer_context, build_service_axilite_ctx,
        build_service_noc_axis_ctx)

    net = NetworkSpecView(enabled_eth=set())
    ctx = {}
    ctx.update(build_service_layer_context(net))
    ctx.update(build_service_axilite_ctx(net))
    ctx.update(build_service_noc_axis_ctx(net))
    ctx["project_name"] = kind
    ctx["service_layer_bd_name"] = f"service_layer_{kind}"
    ctx["dcmac_tcl"] = ""
    ctx["versal_dcmac_root"] = ""

    templates = slash / "linker" / "slashkit" / "resources" / "templates"
    env = Environment(loader=FileSystemLoader(str(templates)), undefined=StrictUndefined,
                      trim_blocks=True, lstrip_blocks=True)
    env.filters["zip"] = lambda a, b: zip(a, b)
    text = env.get_template("service_layer.tcl").render(**ctx)

    block = "\n".join([
        f"# ===== RM contents: {kind} (nic/) =====",
        f"set ::nic(root) {{{root}}}",
        f"source {{{root / 'tcl' / 'nictest_service_layer.tcl'}}}",
        "",
    ])
    block += qnoc_block(root)
    marker = "assign_bd_address -offset 0xE0000000"
    if text.count(marker) != 1:
        raise SystemExit("address assignment marker not found in the service-layer template")
    text = text.replace(marker, block + marker)
    # the user-region host path gets the whole 256 MiB the static routes
    # (M_QDMA_SLV_BRIDGE APERTURES), not the template's 64 KiB
    host = ("assign_bd_address -offset 0xE0000000 -range 0x00010000 "
            "-target_address_space [get_bd_addr_spaces S_QDMA_SLV_BRIDGE]")
    if text.count(host) != 1:
        raise SystemExit("S_QDMA_SLV_BRIDGE address assignment not found in the service-layer template")
    text = text.replace(host, host.replace("-range 0x00010000", "-range 0x10000000"))
    Path(out).write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
