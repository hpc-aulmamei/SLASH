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

import argparse
import logging

from emit.hw.tcl_gen import generate_tcl
from emit.hw.project_gen import create_build_project, generate_image, generate_util_report

import logging

def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s:%(funcName)s: %(message)s",
    )
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
    ap.add_argument("--system-map-template", required=False, default="../resources/system_map.xml",
                    help="Path to system_map.xml Jinja2 template.")
    ap.add_argument("--system-map-out", required=False, default="system_map.xml",
                    help="Path to write system_map.xml (default: ../results/<project>/system_map.xml).")
    ap.add_argument("--proj-root", default=None,
                   help="Project root (defaults to parent of src/).")
    ap.add_argument("-p", "--project", required=True, help="Project name to suffix TCLs and BD clones.")
    ap.add_argument("--clock-hz", required=False, type=int,
                    help="System clock frequency in Hz for system_map.xml (default: from [clock], else 200000000).")
    ap.add_argument("--emit-sw-emu", action="store_true",
                    help="Generate sw_emu tb.cpp (ZMQ HLS simulation server).")
    ap.add_argument("--tb-template", default="../resources/sw_emu/tb.cpp",
                    help="Path to tb.cpp Jinja2 template.")
    ap.add_argument("--tb-out", default=None,
                    help="Output tb.cpp path (default: ../results/<project>/sw_emu/tb.cpp).")
    ap.add_argument("--ip-repository", required=False, default=None,
                    help="Optional IP repository path (string, stored for project generation).")

    args = ap.parse_args()
    generate_tcl(args)
    # if not args.emit_sw_emu:
    #     create_build_project(
    #         project_name=args.project,
    #         ip_repository=args.ip_repository,
    #     )
    generate_image(project_name=args.project)
    generate_util_report(project_name=args.project)

if __name__ == "__main__":
    main()
