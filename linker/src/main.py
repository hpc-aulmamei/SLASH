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
import shutil

from emit.hw.tcl_gen import generate_tcl
from emit.hw.project_gen import (
    create_build_project,
    generate_base_pdi_with_aved,
    generate_image,
    generate_util_report,
)
from emit.sim.tcl_gen import generate_sim_tcl
from emit.emu.tcl_gen import generate_emu_tcl
from emit.sim.project_gen import create_sim_project, build_sim_project
from emit.emu.project_gen import build_emu_project, package_emu_artifacts

from emit.metadata.prog_image import build_vbin

HW_STEPS = (
    "create_project",
    "generate_tcl",
    "create_hw_project",
    "build_hw_project",
    "create_metadata",
)

from emit.hw.tcl_gen import generate_tcl
from emit.hw.project_gen import (
    build_service_layer_rm,
    build_slash_rm,
    create_build_project,
    generate_base_pdi_with_aved,
    generate_image,
    generate_util_report,
    install_abstract_shell,
)
from emit.sim.tcl_gen import generate_sim_tcl
from emit.emu.tcl_gen import generate_emu_tcl
from emit.sim.project_gen import create_sim_project, build_sim_project
from emit.emu.project_gen import build_emu_project, package_emu_artifacts

from emit.metadata.prog_image import build_vbin
from parser.config_parser import parse_connectivity_file

HW_STEPS = (
    "create_project",
    "generate_tcl",
    "create_hw_project",
    "build_hw_project",
    "create_metadata",
)

SIM_STEPS = (
    "create_project",
    "generate_tcl",
    "create_hw_project",
    "build_hw_project",
    "create_metadata",
)

EMU_STEPS = (
    "create_project",
    "generate_tcl",
    "create_hw_project",
    "build_hw_project",
    "create_metadata",
)


def _stage_key(platform: str) -> str:
    if platform == "sim":
        return "sim_stage"
    if platform == "emu":
        return "emu_stage"
    return "hw_stage"


def _steps_for_platform(platform: str) -> tuple[str, ...]:
    if platform == "sim":
        return SIM_STEPS
    if platform == "emu":
        return EMU_STEPS
    return HW_STEPS


def _linker_info_path(project_name: str) -> Path:
    return Path(__file__).resolve().parents[1] / "results" / project_name / ".linker_info.json"


def _save_linker_info(args: argparse.Namespace, stage: str) -> Path:
    steps = _steps_for_platform(args.platform)
    if stage not in steps:
        raise ValueError(f"Unknown stage '{stage}'. Expected one of: {', '.join(steps)}")

    out_path = _linker_info_path(args.project)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    args_payload = {k: v for k, v in vars(args).items() if k not in {"command", "func"}}

    payload: dict = {}
    if out_path.exists():
        with out_path.open("r", encoding="utf-8") as f:
            payload = json.load(f)

    # Merge args to avoid clobbering stored config when stage subcommands are used.
    merged_args = dict(payload.get("args", {}))
    for k, v in args_payload.items():
        if v is None and k in merged_args:
            continue
        merged_args[k] = v
    payload["args"] = merged_args
    payload[_stage_key(args.platform)] = stage
    payload["stage"] = stage  # legacy field

    with out_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
    return out_path


def _load_linker_info(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    if "args" not in payload:
        raise ValueError(f"Missing 'args' in linker info: {path}")
    # Back-compat: if only legacy "stage" exists, mirror to hw/sim stage.
    if "hw_stage" not in payload and "sim_stage" not in payload and "emu_stage" not in payload and "stage" in payload:
        payload["hw_stage"] = payload["stage"]
        payload["sim_stage"] = payload["stage"]
        payload["emu_stage"] = payload["stage"]
    if "emu_stage" not in payload:
        if "hw_stage" in payload:
            payload["emu_stage"] = payload["hw_stage"]
        elif "stage" in payload:
            payload["emu_stage"] = payload["stage"]
    if "emu_stage" in payload and payload["emu_stage"] not in EMU_STEPS:
        payload["emu_stage"] = "create_project"
    if "hw_stage" not in payload and "sim_stage" not in payload and "emu_stage" not in payload and "stage" not in payload:
        raise ValueError(f"Missing stage in linker info: {path}")
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


def _stage_index(stage: str, platform: str) -> int:
    steps = _steps_for_platform(platform)
    if stage not in steps:
        raise ValueError(f"Unknown stage '{stage}'. Expected one of: {', '.join(steps)}")
    return steps.index(stage)

def _stage_requirements(stage: str) -> list[str]:
    if stage == "init":
        return []
    if stage == "complete_hw_build":
        return ["create_project", "generate_tcl"]
    steps = _steps_for_platform("hw")
    if stage not in steps:
        return []
    idx = steps.index(stage)
    return list(steps[:idx])


def _format_stage_requirements(stage: str) -> str:
    req = _stage_requirements(stage)
    if not req:
        return "Requires prior stages: none"
    return "Requires prior stages: " + " -> ".join(req)


def _stage_help_block(stage: str) -> str:
    lines = [f"Stage: {stage}", _format_stage_requirements(stage)]
    if stage == "init":
        lines.append("Writes/updates .linker_info.json with current args.")
    elif stage == "complete_hw_build":
        lines.append("Marks linker build stage complete when HW build was run externally.")
    else:
        lines.append("Uses linker info to resume and validates required stage(s).")
    return "\n".join(lines)


def _all_stage_help_block() -> str:
    stages = ["init", "generate_tcl", "create_hw_project", "build_hw_project", "create_metadata"]
    lines = ["Stages:"]
    for stage in stages:
        lines.append(f"  {stage}: {_format_stage_requirements(stage)}")
    lines.append(f"  complete_hw_build: {_format_stage_requirements('complete_hw_build')} (state-only)")
    lines.append("Use `main.py <stage> --help` for details.")
    return "\n".join(lines)


def _require_stage(payload: dict, required_stage: str, path: Path, platform: str) -> None:
    current = payload.get(_stage_key(platform))
    if current is None:
        raise ValueError(f"Missing stage for platform '{platform}' in linker info: {path}")
    if _stage_index(current, platform) < _stage_index(required_stage, platform):
        raise ValueError(
            f"Linker info {platform} stage '{current}' is before required stage "
            f"'{required_stage}' in {path}"
        )


def _add_common_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--cfg", required=True, help="Path to connectivity config file (e.g., config.cfg).")
    ap.add_argument("--kernels", required=True, nargs="+",
                    help="List of component.xml files to load as kernel types.")
    ap.add_argument("--platform", choices=["hw", "sim", "emu"], default="hw",
                    help="Target platform (hw, sim, or emu).")
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
    ap.add_argument("--sim-template", default="../resources/sim/sim_prj.tcl",
                    help="Path to simulation Tcl template.")
    ap.add_argument("--sim-out", default="run_pre.tcl",
                    help="Path to write simulation Tcl (default: results/<project>/sim/run_pre.tcl).")
    ap.add_argument("--sim-mem", default="../resources/sim/sim_mem.v",
                    help="Path to sim_mem.v for simulation project.")
    ap.add_argument("--system-map-template", required=False, default="../resources/system_map.xml",
                    help="Path to system_map.xml Jinja2 template.")
    ap.add_argument("--system-map-out", required=False, default="system_map.xml",
                    help="Path to write system_map.xml (default: ../results/<project>/system_map.xml).")
    ap.add_argument("--proj-root", default=None,
                   help="Project root (defaults to parent of src/).")
    ap.add_argument("-p", "--project", required=True, help="Project name to suffix TCLs and BD clones.")
    ap.add_argument("--clock-hz", required=False, type=int,
                    help="System clock frequency in Hz for system_map.xml (default: from [clock], else 200000000).")
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
    ap.add_argument("--platform", choices=["hw", "sim", "emu"], default=None,
                    help="Override platform for this stage (hw, sim, or emu).")
    ap.add_argument("--sim", dest="platform", action="store_const", const="sim",
                    help="Shorthand for --platform sim.")
    ap.add_argument("--emu", dest="platform", action="store_const", const="emu",
                    help="Shorthand for --platform emu.")


def _add_rm_build_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--install-dir", default="/opt/amd/slash",
                    help="Installed shell asset directory (default: /opt/amd/slash).")
    ap.add_argument("--vivado-bin", default="vivado",
                    help="Vivado binary to run (default: vivado).")
    ap.add_argument("--jobs", type=int, default=8,
                    help="Parallel jobs for Vivado runs (default: 8).")
    ap.add_argument("--workdir", default=None,
                    help="Optional working directory for Vivado invocation (defaults to SLASH_HW_BUILD_DIR).")
    ap.add_argument("--force", action="store_true",
                    help="Force the RM build even if the cfg would normally skip it (service-layer only).")


def _resolve_platform(payload: dict, args: argparse.Namespace) -> str:
    if getattr(args, "platform", None):
        return args.platform
    payload_args = payload.get("args", {})
    if payload_args.get("platform"):
        return payload_args["platform"]
    return "hw"


def _hw_has_enabled_eth(cfg_path: str | None) -> bool:
    if not cfg_path:
        return False
    cfg = parse_connectivity_file(cfg_path)
    net = getattr(cfg, "network", None)
    enabled_eth = getattr(net, "enabled_eth", set()) if net is not None else set()
    return bool(enabled_eth)


def _stage_init(args: argparse.Namespace) -> None:
    _save_linker_info(args, stage="create_project")


def _stage_generate_tcl(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="create_project", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    if info_args.platform == "sim":
        generate_sim_tcl(info_args)
    elif info_args.platform == "emu":
        generate_emu_tcl(info_args)
    else:
        generate_tcl(info_args)
    _save_linker_info(info_args, stage="generate_tcl")


def _stage_create_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="generate_tcl", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    if info_args.platform == "sim":
        create_sim_project(
            project_name=info_args.project,
            component_xmls=info_args.kernels,
            sim_tcl=Path(info_args.sim_out),
        )
    elif info_args.platform == "emu":
        pass
    else:
        create_build_project(
            project_name=info_args.project,
            ip_repository=info_args.ip_repository,
            action="create",
        )
    _save_linker_info(info_args, stage="create_hw_project")


def _stage_build_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="create_hw_project", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    if info_args.platform == "sim":
        build_sim_project(project_name=info_args.project)
    elif info_args.platform == "emu":
        build_emu_project(
            project_name=info_args.project,
            component_xmls=info_args.kernels,
            tb_cpp=Path(info_args.tb_out) if getattr(info_args, "tb_out", None) else None,
        )
    else:
        create_build_project(
            project_name=info_args.project,
            ip_repository=info_args.ip_repository,
            action="build",
        )
        generate_base_pdi_with_aved(project_name=info_args.project)
    _save_linker_info(info_args, stage="build_hw_project")


def _stage_complete_hw_build(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="generate_tcl", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _save_linker_info(info_args, stage="build_hw_project")


def _build_service_layer_rm(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    if platform != "hw":
        raise ValueError("build_service_layer_rm is only supported for --platform hw")
    _require_stage(payload, required_stage="generate_tcl", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    ip_repository = getattr(info_args, "ip_repository", None)
    if not ip_repository:
        raise ValueError("Missing ip_repository in linker info; run init with --ip-repository")
    if not args.force and not _hw_has_enabled_eth(getattr(info_args, "cfg", None)):
        print(f"INFO: No eth_* enabled in cfg for project '{info_args.project}'. Skipping service_layer RM build.")
        return
    build_service_layer_rm(
        project_name=info_args.project,
        ip_repository=ip_repository,
        install_dir=Path(args.install_dir),
        vivado_bin=args.vivado_bin,
        workdir=Path(args.workdir) if args.workdir else None,
        jobs=args.jobs,
    )


def _build_slash_rm(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    if platform != "hw":
        raise ValueError("build_slash_rm is only supported for --platform hw")
    _require_stage(payload, required_stage="generate_tcl", path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    ip_repository = getattr(info_args, "ip_repository", None)
    if not ip_repository:
        raise ValueError("Missing ip_repository in linker info; run init with --ip-repository")
    build_slash_rm(
        project_name=info_args.project,
        ip_repository=ip_repository,
        install_dir=Path(args.install_dir),
        vivado_bin=args.vivado_bin,
        workdir=Path(args.workdir) if args.workdir else None,
        jobs=args.jobs,
    )


def _stage_create_metadata(args: argparse.Namespace) -> None:
    info_path = Path(args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    required_stage = "build_hw_project"
    _require_stage(payload, required_stage=required_stage, path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    if info_args.platform == "sim":
        pass
    elif info_args.platform == "emu":
        package_emu_artifacts(project_name=info_args.project)
    else:
        include_service_layer = _hw_has_enabled_eth(getattr(info_args, "cfg", None))
        generate_image(project_name=info_args.project, include_service_layer=include_service_layer)
        generate_util_report(project_name=info_args.project)
        build_vbin(project_name=info_args.project)
    _save_linker_info(info_args, stage="create_metadata")


_STAGE_FUNCS = {
    "create_project": _stage_init,
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
    platform = _resolve_platform(payload, args)
    current_stage = payload.get(_stage_key(platform))
    steps = _steps_for_platform(platform)
    if current_stage not in steps:
        raise ValueError(f"Invalid or missing stage in linker info: {info_path}")

    current_idx = _stage_index(current_stage, platform)
    target_idx = _stage_index(target_stage, platform)

    rerun_target_only = False
    if current_idx >= target_idx:
        print(f"Project is already in {current_stage} state. Do you wish to rerun? Y/N.")
        try:
            resp = input().strip().lower()
        except EOFError:
            resp = ""
        if resp not in {"y", "yes"}:
            return
        rerun_target_only = True

    if rerun_target_only:
        stages_to_run = [target_stage]
    else:
        # Resume from the next unfinished stage instead of restarting from scratch.
        stages_to_run = list(steps[current_idx + 1:target_idx + 1])
    for stage in stages_to_run:
        func = _STAGE_FUNCS.get(stage)
        if func is None:
            raise ValueError(f"Stage '{stage}' has no runnable command.")
        _run_step(stage, lambda f=func: f(args))

def _install(args: argparse.Namespace) -> None:
    print("DISCLAIMER: Run this install flow only once. It will take a long time.", flush=True)
    print("INFO: You may be prompted for sudo only at the final copy step.", flush=True)
    install_abstract_shell(
        project_name=args.project,
        ip_repository=args.ip_repository,
        install_dir=Path(args.install_dir),
        vivado_bin=args.vivado_bin,
        workdir=Path(args.workdir) if args.workdir else None,
    )


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s:%(funcName)s: %(message)s",
    )
    if len(sys.argv) > 1 and sys.argv[1] in {"init", "generate_tcl", "create_hw_project", "build_hw_project", "build_service_layer_rm", "build_slash_rm", "complete_hw_build", "create_metadata", "install"}:
        ap = argparse.ArgumentParser(
            description="Linker stages (use linker info to resume).",
            epilog=_all_stage_help_block(),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        sub = ap.add_subparsers(dest="command", required=True)

        ap_init = sub.add_parser(
            "init",
            help="Save linker args and initialize stage state.",
            description="Initialize linker state for staged execution.",
            epilog=_stage_help_block("init"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_common_args(ap_init)
        ap_init.set_defaults(func=_stage_init)

        ap_gen = sub.add_parser(
            "generate_tcl",
            help="Generate Tcl from saved linker args.",
            description="Generate Tcl using previously saved linker args.",
            epilog=_stage_help_block("generate_tcl"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_gen)
        ap_gen.set_defaults(func=_stage_generate_tcl)

        ap_create = sub.add_parser(
            "create_hw_project",
            help="Create hardware project from generated Tcl.",
            description="Create hardware project using generated Tcl.",
            epilog=_stage_help_block("create_hw_project"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_create)
        ap_create.set_defaults(func=_stage_create_hw_project)

        ap_build = sub.add_parser(
            "build_hw_project",
            help="Build hardware project from existing design.",
            description="Build hardware project from an existing design.",
            epilog=_stage_help_block("build_hw_project"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_build)
        ap_build.set_defaults(func=_stage_build_hw_project)

        ap_complete = sub.add_parser(
            "complete_hw_build",
            help="Mark external HW build complete and advance linker stage state.",
            description="Mark build_hw_project stage complete when Vivado build ran outside linker.",
            epilog=_stage_help_block("complete_hw_build"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_complete)
        ap_complete.set_defaults(func=_stage_complete_hw_build)

        ap_service_rm = sub.add_parser(
            "build_service_layer_rm",
            help="Build the service-layer RM using installed shell assets and generated Tcl.",
            description="Run service_layer_build.tcl for the project using linker info + SLASH_HW_BUILD_DIR.",
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_service_rm)
        _add_rm_build_args(ap_service_rm)
        ap_service_rm.set_defaults(func=_build_service_layer_rm)

        ap_slash_rm = sub.add_parser(
            "build_slash_rm",
            help="Build the slash RM using installed shell assets and generated Tcl.",
            description="Run slash_project_build.tcl for the project using linker info + SLASH_HW_BUILD_DIR.",
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_slash_rm)
        _add_rm_build_args(ap_slash_rm)
        ap_slash_rm.set_defaults(func=_build_slash_rm)

        ap_meta = sub.add_parser(
            "create_metadata",
            help="Generate images/utilization/vbin artifacts.",
            description="Generate images, utilization, and vbin artifacts.",
            epilog=_stage_help_block("create_metadata"),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_linker_info_args(ap_meta)
        ap_meta.set_defaults(func=_stage_create_metadata)

        ap_install = sub.add_parser(
            "install",
            help="Build/install base shell artifacts and AVED PDI.",
            description="Run create_project.tcl, install shell DCPs, then run AVED flow and install the base PDI.",
            formatter_class=argparse.RawTextHelpFormatter,
        )
        ap_install.add_argument("-p", "--project", default="abs_shell",
                                help="Project name to pass to create_project.tcl (default: abs_shell).")
        ap_install.add_argument("--ip-repository", required=False, default=None,
                                help="Optional IP repository path to pass to create_project.tcl.")
        ap_install.add_argument("--install-dir", default="/opt/amd/slash",
                                help="Directory to install shell DCP/PDI artifacts to (default: /opt/amd/slash).")
        ap_install.add_argument("--vivado-bin", default="vivado",
                                help="Vivado binary to run (default: vivado).")
        ap_install.add_argument("--workdir", default=None,
                                help="Optional working directory for Vivado invocation.")
        ap_install.set_defaults(func=_install)

        args = ap.parse_args()
        if args.command == "init":
            _run_step("init", lambda: args.func(args))
        elif args.command == "build_service_layer_rm":
            _run_step("build_service_layer_rm", lambda: args.func(args))
        elif args.command == "build_slash_rm":
            _run_step("build_slash_rm", lambda: args.func(args))
        elif args.command == "complete_hw_build":
            _run_step("complete_hw_build", lambda: args.func(args))
        elif args.command == "install":
            _run_step("install", lambda: args.func(args))
        else:
            _run_from_last_to_target(args, args.command)
    else:
        ap = argparse.ArgumentParser(
            description="Parse kernels (component.xml), connectivity config, BD port map, and render Tcl.",
            epilog=_all_stage_help_block(),
            formatter_class=argparse.RawTextHelpFormatter,
        )
        _add_common_args(ap)
        args = ap.parse_args()
        _run_step("create_project", lambda: _save_linker_info(args, stage="create_project"))
        def _do_generate_tcl() -> None:
            if args.platform == "sim":
                generate_sim_tcl(args)
            elif args.platform == "emu":
                generate_emu_tcl(args)
            else:
                generate_tcl(args)
            _save_linker_info(args, stage="generate_tcl")
        _run_step("generate_tcl", _do_generate_tcl)
        def _do_build_all() -> None:
            if args.platform == "sim":
                create_sim_project(
                    project_name=args.project,
                    component_xmls=args.kernels,
                    sim_tcl=Path(args.sim_out),
                )
                build_sim_project(project_name=args.project)
            elif args.platform == "emu":
                build_emu_project(
                    project_name=args.project,
                    component_xmls=args.kernels,
                    tb_cpp=Path(args.tb_out) if getattr(args, "tb_out", None) else None,
                )
            else:
                create_build_project(
                project_name=args.project,
                ip_repository=args.ip_repository,
                action="all")
                generate_base_pdi_with_aved(project_name=args.project)
            _save_linker_info(args, stage="build_hw_project")
        _run_step("build", _do_build_all)
        def _do_create_metadata() -> None:
            if args.platform == "sim":
                pass
            elif args.platform == "emu":
                package_emu_artifacts(project_name=args.project)
            else:
                generate_image(project_name=args.project)
                generate_util_report(project_name=args.project)
                build_vbin(project_name=args.project)
            _save_linker_info(args, stage="create_metadata")
        _run_step("create_metadata", _do_create_metadata)

if __name__ == "__main__":
    main()
