# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
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

from __future__ import annotations

from pathlib import Path
import logging
import re

from emit.render import render_template
from emit.metadata.system_map_ctx import build_system_map_context, resolve_system_map_clock
from emit.hw.user_region.addr_ctx import build_axilite_address_context
from emit.emu.tb_ctx import build_tb_context, infer_sol1_json_from_component_xml

from parser.component_parser import parse_component_xml
from parser.config_parser import parse_connectivity_file, apply_config_to_instances

logger = logging.getLogger(__name__)


def _sanitize_project_name(s: str) -> str:
    # Keep letters/digits/underscore; don’t start with a digit.
    s2 = re.sub(r"[^A-Za-z0-9_]+", "_", s.strip())
    if not s2:
        s2 = "proj"
    if s2[0].isdigit():
        s2 = "_" + s2
    return s2


def _results_root() -> Path:
    # linker/src/emit/emu -> linker/results
    return Path(__file__).resolve().parents[3] / "results"


def generate_emu_tcl(args) -> None:
    args.tb_template = getattr(args, "tb_template", "../resources/sw_emu/tb.cpp")
    args.system_map_out = getattr(args, "system_map_out", "system_map.xml")
    args.system_map_template = getattr(args, "system_map_template", "../resources/system_map.xml")

    project = _sanitize_project_name(args.project)
    results_root = _results_root()
    emu_root = results_root / project / "sw_emu"
    emu_root.mkdir(parents=True, exist_ok=True)

    if getattr(args, "tb_out", None) is None:
        args.tb_out = str(emu_root / "tb.cpp")

    default_system_map_out = results_root / project / "system_map.xml"
    if args.system_map_out == "system_map.xml":
        args.system_map_out = str(default_system_map_out)

    # 1) Parse kernels
    kernel_library = {}
    kernel_compxml_by_type = {}
    for kpath in args.kernels:
        kfile = Path(kpath)
        if not kfile.exists():
            raise FileNotFoundError(f"Kernel file not found: {kfile}")
        k = parse_component_xml(kfile)
        kernel_library[k.name] = k
        kernel_compxml_by_type[k.name] = kfile.resolve()

    # 2) Parse connectivity config
    cfg = parse_connectivity_file(args.cfg)

    # 3) Make instances & stream edges
    instances, streams = apply_config_to_instances(cfg, kernel_library)

    # 4) Build tb.cpp context from HLS metadata
    kernel_hls_by_type = {
        ktype: infer_sol1_json_from_component_xml(Path(comp_xml))
        for ktype, comp_xml in kernel_compxml_by_type.items()
    }
    tb_ctx = build_tb_context(instances, streams, kernel_hls_by_type)

    tb_template = Path(args.tb_template)
    tb_out = Path(args.tb_out)
    tb_out.parent.mkdir(parents=True, exist_ok=True)
    render_template(
        template_dir=tb_template.parent,
        template_name=tb_template.name,
        out_path=tb_out,
        context=tb_ctx,
    )
    logger.info("Rendered sw_emu tb.cpp to %s", tb_out)

    # 5) Render system map (Emulation)
    axilite_ctx = build_axilite_address_context(
        instances,
        addr_space="S_AXILITE_INI",
        base_offset=0x0202_0000_0000,
        min_align=0x0001_0000,
    )
    clock_hz = resolve_system_map_clock(args.clock_hz, instances)
    system_map_ctx = build_system_map_context(
        instances,
        axilite_ctx.get("axilite_addr", []),
        clock_hz=clock_hz,
        platform="Emulation",
        network=getattr(cfg, "network", None),
    )
    system_map_template = Path(args.system_map_template)
    system_map_out = Path(args.system_map_out)
    system_map_out.parent.mkdir(parents=True, exist_ok=True)
    render_template(
        template_dir=system_map_template.parent,
        template_name=system_map_template.name,
        out_path=system_map_out,
        context=system_map_ctx,
    )
    logger.info("Rendered system map to %s", system_map_out)
