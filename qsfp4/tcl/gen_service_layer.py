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

"""Generate service_layer.tcl for the four-port Ethernet test RM.

The SLASH service-layer template provides the reconfigurable module's NoC
boundary (S_AXILITE_INI, the DCMAC INIS ports, SL2NOC, the virtual NoC and QDMA
bridge pass-throughs). It is rendered with no Ethernet ports, so it creates
neither DCMACs nor the control SmartConnect; tcl/qsfp_service_layer.tcl is
inserted before the address assignment and adds both DCMACs in dual mode, the
four port testers, their clocks and the control path.

usage: gen_service_layer.py <SLASH checkout> <out.tcl> <qsfp4 dir> <Versal-DCMAC dir>
"""
import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined


def main():
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    slash, out, root, versal = (Path(a).resolve() for a in sys.argv[1:5])

    sys.path.insert(0, str(slash / "linker"))
    from slashkit.emit.hw.service_region.service_layer_ctx import (
        NetworkSpecView, build_service_layer_context, build_service_axilite_ctx,
        build_service_noc_axis_ctx)

    net = NetworkSpecView(enabled_eth=set())
    ctx = {}
    ctx.update(build_service_layer_context(net))
    ctx.update(build_service_axilite_ctx(net))
    ctx.update(build_service_noc_axis_ctx(net))
    ctx["project_name"] = "qsfploop"
    ctx["service_layer_bd_name"] = "service_layer_qsfploop"
    ctx["dcmac_tcl"] = ""
    ctx["versal_dcmac_root"] = ""

    templates = slash / "linker" / "slashkit" / "resources" / "templates"
    env = Environment(loader=FileSystemLoader(str(templates)), undefined=StrictUndefined,
                      trim_blocks=True, lstrip_blocks=True)
    env.filters["zip"] = lambda a, b: zip(a, b)
    text = env.get_template("service_layer.tcl").render(**ctx)

    wrapper = slash / "linker" / "slashkit" / "resources" / "dcmac" / "tcl" / "slash_wrapper.tcl"
    insert = "\n".join([
        "# ===== four-port Ethernet test (qsfp4/) =====",
        f"set ::qsfp4(root) {{{root}}}",
        f"set ::qsfp4(versal_dcmac) {{{versal}}}",
        f"set ::qsfp4(slash_wrapper) {{{wrapper}}}",
        f"source {{{root / 'tcl' / 'qsfp_service_layer.tcl'}}}",
        "",
    ])
    marker = "assign_bd_address -offset 0xE0000000"
    if text.count(marker) != 1:
        raise SystemExit("address assignment marker not found in the service-layer template")
    text = text.replace(marker, insert + marker)
    Path(out).write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
