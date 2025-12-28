# main.py
import argparse
from collections import OrderedDict
from pathlib import Path

from emit.kernel_ctx import build_kernel_add_context
from emit.smartconnect_ctx import build_axilite_smartconnect_context
from emit.hbm_ctx import build_hbm_smartconnect_context
from emit.ddr_ctx import build_ddr_smartconnect_context
from emit.mem_ctx import build_mem_smartconnect_context
from emit.render import render_template
from emit.virt_ctx import build_virt_smartconnect_context
from emit.terminator_ctx import build_axi_terminators_context
from emit.terminator_ctx import build_ddr_noc_terminators
from emit.terminator_ctx import build_mem_noc_terminators
from parser.component_parser import parse_component_xml
from emit.network_ctx import build_network_axis_context
from emit.stream_ctx import build_stream_connect_context
from emit.host_ctx import build_host_smartconnect_context
from emit.addr_ctx import build_axilite_address_context

# Service layer
from emit.service_layer_ctx import *

from parser.config_parser import (
    parse_connectivity_file,
    apply_config_to_instances,
)
from core.bd_ports import load_bd_ports_from_file
from core.port import PortType

def _collect_used_targets(ctx: dict) -> set[str]:
    used: set[str] = set()

    # HBM uses ports
    for item in ctx.get("hbm_direct", []):
        used.add(item["dst_port"])
    for item in ctx.get("hbm_smart_roots", []):
        used.add(item["dst_port"])

    # DDR uses NoC pins
    for item in ctx.get("ddr_direct", []):
        used.add(item["dst_pin"])
    for item in ctx.get("ddr_smart_roots", []):
        used.add(item["dst_pin"])

    # MEM uses NoC pins
    for item in ctx.get("mem_direct", []):
        used.add(item["dst_pin"])
    for item in ctx.get("mem_smart_roots", []):
        used.add(item["dst_pin"])

    # VIRT uses ports (like HBM)
    for item in ctx.get("virt_direct", []):
        used.add(item["dst_pin"])
    for item in ctx.get("virt_smart_roots", []):
        used.add(item["dst_pin"])

    return used

# --- add this helper somewhere in main.py ---
def print_memory_maps(k):
    if not getattr(k, "memory_maps", None):
        print("  (no memory maps)")
        return
    print("  Memory maps:")
    for mm in k.memory_maps:
        print(f"    - map: {mm.name}")
        for ab in mm.address_blocks:
            ba = f"0x{ab.base_address:X}"
            rg = f"0x{ab.range:X}"
            print(f"        block {ab.name}: base={ba} range={rg} width={ab.width} usage={ab.usage or '-'} access={ab.access or '-'}")
            if ab.offset_base_param or ab.offset_high_param:
                print(f"          params: base_param={ab.offset_base_param or '-'} high_param={ab.offset_high_param or '-'}")
            if ab.registers:
                for r in ab.registers:
                    off = f"0x{r.address_offset:X}"
                    print(f"          reg {r.name}: off={off} size={r.size} access={r.access or '-'} reset={('0x%X' % r.reset_value) if r.reset_value is not None else '-'}")
                    if r.fields:
                        for f in r.fields:
                            rng = f"[{f.bit_offset + f.bit_width - 1}:{f.bit_offset}]"
                            print(f"            - {f.name} {rng} access={f.access or '-'}"
                                  f" reset={('0x%X' % f.reset_value) if f.reset_value is not None else '-'}")


def print_kernel(k):
    print(f"\nKernel: {k.name}")
    for p in k.ports.values():
        print(f"  - {p.name:24s} {p.ptype.name:9s} width={p.width}")
    print_memory_maps(k)



def print_cfg(cfg):
    print("\n[connectivity] nk entries:")
    if cfg.nk:
        for nk in cfg.nk:
            print(f"  - {nk.kernel_type}: count={nk.count}, names={nk.instance_names}")
    else:
        print("  (none)")

    print("\n[connectivity] stream_connect:")
    if cfg.streams:
        for s in cfg.streams:
            print(f"  - {s.src_inst}.{s.src_port} -> {s.dst_inst}.{s.dst_port}")
    else:
        print("  (none)")

    print("\n[connectivity] sp mappings:")
    if cfg.sps:
        for sp in cfg.sps:
            print(f"  - {sp.inst}.{sp.port} -> {sp.target.domain}{sp.target.index}")
    else:
        print("  (none)")

    print("\n[clock] specs:")
    if cfg.clocks:
        for c in cfg.clocks:
            print(f"  - {c.inst}: {c.freq_hz} Hz")
    else:
        print("  (none)")


def print_instances(instances, stream_edges):
    print("\nInstances created:")
    if not instances:
        print("  (none)")
        return
    for name, inst in instances.items():
        print(f"  - {name} : kernel={inst.kernel.name}")
        if inst.params:
            clk = inst.params.get("clock_hz")
            if clk is not None:
                print(f"      clock_hz: {clk}")
            mem_sp = inst.params.get("mem_sp")
            if mem_sp:
                for port, tgt in mem_sp.items():
                    idx = "" if tgt.get("index") is None else str(tgt["index"])
                    print(f"      sp: {port} -> {tgt['domain']}{idx}")
            others = {k: v for k, v in inst.params.items() if k not in {"clock_hz", "mem_sp"}}
            for k, v in others.items():
                print(f"      {k}: {v}")

    print("\nStream connections to wire:")
    if stream_edges:
        for s in stream_edges:
            print(f"  - {s.src_inst}.{s.src_port} -> {s.dst_inst}.{s.dst_port}")
    else:
        print("  (none)")


def print_bd_ports(bd):
    print("\nBlock Design Ports:")
    if not bd.ports:
        print("  (none)")
        return
    for logical in sorted(bd.ports.keys()):
        for p in bd.get_all(logical):
            dom = "" if p.domain is None else str(p.domain)
            idx = "" if p.index is None else str(p.index)
            wid = "" if p.width is None else str(p.width)
            rtl = "" if p.rtl_name is None else p.rtl_name
            print(f"  - {logical:12s} -> rtl={rtl:20s} {p.ptype.name:9s} width={wid:>4s} domain={dom:>4s} index={idx:>2s}")

def main():
    ap = argparse.ArgumentParser(
        description="Parse kernels (component.xml), connectivity config, BD port map, and render Tcl."
    )
    ap.add_argument("--cfg", required=True, help="Path to connectivity config file (e.g., config.cfg).")
    ap.add_argument("--kernels", required=True, nargs="+",
                    help="List of component.xml files to load as kernel types.")
    ap.add_argument("--bd-ports", required=False, default="../resources/bd_ports.txt",
                    help="Path to BD ports mapping file (logical:rtl TYPE [width]).")
    ap.add_argument("--template", default="../resources/slash.tcl",
                    help="Path to Jinja2 Tcl template (default: ../resources/slash.tcl).")
    ap.add_argument("--out", default="slash.tcl",
                    help="Path to write rendered Tcl (default: slash.tcl).")
    ap.add_argument("--service-template", required=False, default="../resources/service_layer.tcl",
                help="Path to service layer Jinja2 template (e.g., resources/service_layer.tcl)")
    ap.add_argument("--service-out", required=False, default="service_layer_gen.tcl",
                    help="Path to write rendered service layer Tcl (e.g., build/service_layer.tcl)")
    ap.add_argument("--proj-root", default=None,
                   help="Project root (defaults to parent of src/).")
    args = ap.parse_args()

    # 0) Load BD ports and print
    bd = load_bd_ports_from_file(args.bd_ports)
    print_bd_ports(bd)

    # 1) Parse kernels
    kernel_library = {}
    print("\nLoading kernels:")
    for kpath in args.kernels:
        kfile = Path(kpath)
        if not kfile.exists():
            raise FileNotFoundError(f"Kernel file not found: {kfile}")
        k = parse_component_xml(kfile)
        kernel_library[k.name] = k
        print_kernel(k)

    # 2) Parse connectivity config
    cfg = parse_connectivity_file(args.cfg)
    print_cfg(cfg)

    # 3) Make instances & stream edges
    instances, streams = apply_config_to_instances(cfg, kernel_library)
    print_instances(instances, streams)

    # 4) Build context for kernel adds (+clocks/resets) and render
    ctx = build_kernel_add_context(instances)
    ctx.update(build_axilite_smartconnect_context(instances))
    ctx.update(build_hbm_smartconnect_context(instances, bd, max_si=16))
    ctx.update(build_ddr_smartconnect_context(instances, max_si=16))
    ctx.update(build_mem_smartconnect_context(instances, num_mem_ports=8, max_si=16))
    ctx.update(build_host_smartconnect_context(instances, bd, max_si=16))
    ctx.update(build_virt_smartconnect_context(instances, bd, max_si=16))
    net_ctx = build_network_axis_context(instances, streams, cfg.network)
    ctx.update({
    "axis_to_fabric":   net_ctx["axis_to_fabric"],   # inst.AXIS -> /dcmac_axis_noc_k/S00_AXIS
    "axis_from_fabric": net_ctx["axis_from_fabric"], # /dcmac_axis_noc_s_k/M00_AXIS -> inst.AXIS
    })

    ctx.update(build_stream_connect_context(instances, net_ctx["streams_leftover"]))

    used_targets = _collect_used_targets(ctx)

    terms_generic = build_axi_terminators_context(bd, used_targets)  # HBM/VIRT BD ports only
    terms_ddr_noc = build_ddr_noc_terminators(used_targets, num_ddr=4, noc_pin_fmt="/ddr_noc_{index}/S00_AXI")
    terms_mem_noc = build_mem_noc_terminators(used_targets, num_mem=8, noc_pin_fmt="/hbm_vnoc_0{index}/S00_AXI")
    ctx["axi_terminators"] = (
        terms_generic.get("axi_terminators", [])
        + terms_ddr_noc.get("axi_terminators", [])
        + terms_mem_noc.get("axi_terminators", [])
    )
    ctx.update(
    build_axilite_address_context(
        instances,
        addr_space="S_AXILITE_INI",
        base_offset=0x0202_0000_0000,
        min_align=0x0001_0000,
    )
)
    #ctx.update(build_axi_terminators_context(bd, used_targets))

    template_path = Path(args.template)   # resources/slash.tcl
    out_path = Path(args.out)             # slash.tcl
    render_template(
        template_dir=template_path.parent,
        template_name=template_path.name,
        out_path=out_path,
        context=ctx,
    )    
    print(f"\nRendered Tcl to {out_path}")

    paths_ctx = compute_paths(Path(args.proj_root).resolve() if args.proj_root else None)
    svc_ctx = {}
    svc_ctx.update(build_service_layer_context(cfg.network))
    svc_ctx.update(build_service_axilite_ctx(cfg.network))    # SmartConnect + MI targets
    svc_ctx.update(build_service_noc_axis_ctx(cfg.network))
    svc_ctx.update(paths_ctx)                                 # absolute paths for dcmac sources


    # --- Render service-layer Tcl ---
    svc_template = Path(args.service_template)
    svc_out = Path(args.service_out)
    svc_out.parent.mkdir(parents=True, exist_ok=True)
    render_template(
    template_dir=svc_template.parent,
    template_name=svc_template.name,
    out_path=svc_out,
    context=svc_ctx,
    )

    print(f"Rendered service layer Tcl to {svc_out}")

if __name__ == "__main__":
    main()
