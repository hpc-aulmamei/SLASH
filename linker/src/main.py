# main.py
import argparse

from emit.hw.tcl_gen import generate_tcl
from emit.hw.project_gen import create_build_project, generate_image

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
    # generate_tcl(args)
    # if not args.emit_sw_emu:
    #     create_build_project(
    #         project_name=args.project,
    #         ip_repository=args.ip_repository,
    #     )
    generate_image(project_name=args.project)
if __name__ == "__main__":
    main()
