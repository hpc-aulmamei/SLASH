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
import json
import logging
import os
import sys
import threading
import time
from pathlib import Path

from emit.hw.tcl_gen import generate_tcl
from emit.hw.project_gen import create_build_project, generate_image, generate_util_report

from emit.metadata.prog_image import build_vbin

LINKER_STEPS = (
    "create_project",
    "generate_tcl",
    "create_hw_project",
    "build_hw_project",
    "create_metadata",
)


def _linker_info_path(project_name: str) -> Path:
    return Path(__file__).resolve().parents[1] / "results" / project_name / ".linker_info.json"


def _save_linker_info(args: argparse.Namespace, stage: str) -> Path:
    if stage not in LINKER_STEPS:
        raise ValueError(f"Unknown stage '{stage}'. Expected one of: {', '.join(LINKER_STEPS)}")

    out_path = _linker_info_path(args.project)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    args_payload = {k: v for k, v in vars(args).items() if k not in {"command", "func"}}
    payload = {
        "stage": stage,
        "args": args_payload,
    }
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
    return out_path


def _load_linker_info(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    if "args" not in payload:
        raise ValueError(f"Missing 'args' in linker info: {path}")
    if "stage" not in payload:
        raise ValueError(f"Missing 'stage' in linker info: {path}")
    return payload


def _format_duration(seconds: float) -> str:
    total = int(round(seconds))
    hours = total // 3600
    minutes = (total % 3600) // 60
    secs = total % 60
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


def _run_step(label: str, func) -> None:
    start_wall = time.perf_counter()
    start_cpu = time.process_time()
    start_rusage = None
    cores = os.cpu_count() or 1
    peak_cpu_pct = None
    stop_event = threading.Event()

    def _sample_cpu_peak() -> None:
        nonlocal peak_cpu_pct
        last_wall = time.perf_counter()
        last_cpu = time.process_time()
        while not stop_event.wait(0.2):
            now_wall = time.perf_counter()
            now_cpu = time.process_time()
            delta_wall = now_wall - last_wall
            if delta_wall > 0:
                delta_cpu = now_cpu - last_cpu
                cpu_pct = (delta_cpu / delta_wall) * 100.0
                if peak_cpu_pct is None or cpu_pct > peak_cpu_pct:
                    peak_cpu_pct = cpu_pct
            last_wall = now_wall
            last_cpu = now_cpu

    try:
        import resource
        start_rusage = resource.getrusage(resource.RUSAGE_SELF)
    except Exception:
        start_rusage = None

    sampler = threading.Thread(target=_sample_cpu_peak, daemon=True)
    sampler.start()
    try:
        func()
    finally:
        stop_event.set()
        sampler.join(timeout=1.0)
        end_wall = time.perf_counter()
        end_cpu = time.process_time()
        cpu_str = _format_duration(end_cpu - start_cpu)
        wall_str = _format_duration(end_wall - start_wall)
        avg_cpu_pct = 0.0
        elapsed = end_wall - start_wall
        if elapsed > 0:
            avg_cpu_pct = ((end_cpu - start_cpu) / elapsed) * 100.0
        rss_part = ""
        if start_rusage is not None:
            try:
                import resource
                end_rusage = resource.getrusage(resource.RUSAGE_SELF)
                # ru_maxrss is in kilobytes on Linux; convert to MB.
                rss_mb = end_rusage.ru_maxrss / 1024.0
                rss_part = f" ; max_rss = {rss_mb:.1f} MB"
            except Exception:
                rss_part = ""
        peak_part = ""
        if peak_cpu_pct is not None:
            peak_part = f" ; cpu_peak_pct = {peak_cpu_pct:.1f}"
        print(
            f"{label}: Time (s): cpu = {cpu_str} ; elapsed = {wall_str}"
            f" ; cpu_avg_pct = {avg_cpu_pct:.1f}{peak_part} ; cores = {cores}{rss_part}"
        )


def _stage_index(stage: str) -> int:
    if stage not in LINKER_STEPS:
        raise ValueError(f"Unknown stage '{stage}'. Expected one of: {', '.join(LINKER_STEPS)}")
    return LINKER_STEPS.index(stage)


def _require_stage(payload: dict, required_stage: str, path: Path) -> None:
    current = payload.get("stage")
    if current is None:
        raise ValueError(f"Missing 'stage' in linker info: {path}")
    if _stage_index(current) < _stage_index(required_stage):
        raise ValueError(
            f"Linker info stage '{current}' is before required stage '{required_stage}' in {path}"
        )


def _add_common_args(ap: argparse.ArgumentParser) -> None:
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


def _add_linker_info_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("-p", "--project", required=True, help="Project name to locate linker info.")
    ap.add_argument("--linker-info", default=None,
                    help="Optional path to .linker_info.json (overrides results/<project> lookup).")


def _stage_init(args: argparse.Namespace) -> None:
    _save_linker_info(args, stage="create_project")


def _stage_generate_tcl(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    _require_stage(payload, required_stage="create_project", path=info_path)
    info_args = argparse.Namespace(**payload["args"])
    generate_tcl(info_args)
    _save_linker_info(info_args, stage="generate_tcl")


def _stage_create_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    _require_stage(payload, required_stage="generate_tcl", path=info_path)
    info_args = argparse.Namespace(**payload["args"])
    create_build_project(
        project_name=info_args.project,
        ip_repository=info_args.ip_repository,
        action="create",
    )
    _save_linker_info(info_args, stage="create_hw_project")


def _stage_build_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    _require_stage(payload, required_stage="create_hw_project", path=info_path)
    info_args = argparse.Namespace(**payload["args"])
    create_build_project(
        project_name=info_args.project,
        ip_repository=info_args.ip_repository,
        action="build",
    )
    _save_linker_info(info_args, stage="build_hw_project")


def _stage_create_metadata(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    _require_stage(payload, required_stage="build_hw_project", path=info_path)
    info_args = argparse.Namespace(**payload["args"])
    generate_image(project_name=info_args.project)
    generate_util_report(project_name=info_args.project)
    build_vbin(project_name=info_args.project)
    _save_linker_info(info_args, stage="create_metadata")


_STAGE_FUNCS = {
    "generate_tcl": _stage_generate_tcl,
    "create_hw_project": _stage_create_hw_project,
    "build_hw_project": _stage_build_hw_project,
    "create_metadata": _stage_create_metadata,
}


def _run_from_last_to_target(args: argparse.Namespace, target_stage: str) -> None:
    if target_stage not in _STAGE_FUNCS:
        raise ValueError(f"Unknown target stage '{target_stage}'.")

    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    current_stage = payload.get("stage")
    if current_stage not in LINKER_STEPS:
        raise ValueError(f"Invalid or missing stage in linker info: {info_path}")

    current_idx = _stage_index(current_stage)
    target_idx = _stage_index(target_stage)

    if target_idx <= current_idx:
        stages_to_run = [target_stage]
    else:
        stages_to_run = list(LINKER_STEPS[current_idx + 1:target_idx + 1])

    for stage in stages_to_run:
        func = _STAGE_FUNCS.get(stage)
        if func is None:
            raise ValueError(f"Stage '{stage}' has no runnable command.")
        _run_step(stage, lambda f=func: f(args))


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s:%(funcName)s: %(message)s",
    )
    if len(sys.argv) > 1 and sys.argv[1] in {"init", "generate_tcl", "create_hw_project", "build_hw_project", "create_metadata"}:
        ap = argparse.ArgumentParser(
            description="Linker stages (use linker info to resume)."
        )
        sub = ap.add_subparsers(dest="command", required=True)

        ap_init = sub.add_parser("init", help="Save linker args and initialize stage state.")
        _add_common_args(ap_init)
        ap_init.set_defaults(func=_stage_init)

        ap_gen = sub.add_parser("generate_tcl", help="Generate Tcl from saved linker args.")
        _add_linker_info_args(ap_gen)
        ap_gen.set_defaults(func=_stage_generate_tcl)

        ap_create = sub.add_parser("create_hw_project", help="Create hardware project from generated Tcl.")
        _add_linker_info_args(ap_create)
        ap_create.set_defaults(func=_stage_create_hw_project)

        ap_build = sub.add_parser("build_hw_project", help="Build hardware project from existing design.")
        _add_linker_info_args(ap_build)
        ap_build.set_defaults(func=_stage_build_hw_project)

        ap_meta = sub.add_parser("create_metadata", help="Generate images/utilization/vbin artifacts.")
        _add_linker_info_args(ap_meta)
        ap_meta.set_defaults(func=_stage_create_metadata)

        args = ap.parse_args()
        if args.command == "init":
            _run_step("init", lambda: args.func(args))
        else:
            _run_from_last_to_target(args, args.command)
    else:
        ap = argparse.ArgumentParser(
            description="Parse kernels (component.xml), connectivity config, BD port map, and render Tcl."
        )
        _add_common_args(ap)
        args = ap.parse_args()
        _run_step("create_project", lambda: _save_linker_info(args, stage="create_project"))
        def _do_generate_tcl() -> None:
            generate_tcl(args)
            _save_linker_info(args, stage="generate_tcl")
        _run_step("generate_tcl", _do_generate_tcl)
        def _do_build_all() -> None:
            create_build_project(
            project_name=args.project,
            ip_repository=args.ip_repository,
            action="all")
        _run_step("build", _do_build_all)
        def _do_create_metadata() -> None:
            generate_image(project_name=args.project)
            generate_util_report(project_name=args.project)
            build_vbin(project_name=args.project)
            _save_linker_info(args, stage="create_metadata")
        _run_step("create_metadata", _do_create_metadata)

if __name__ == "__main__":
    main()
