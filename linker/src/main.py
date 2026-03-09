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
from typing import List, Optional

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
from core.command_config import LinkerConfiguration, Platform, InstallerConfiguration, CommandConfiguration
from emit.metadata.timing_freq import apply_timing_frequency_cap
from parser.config_parser import parse_connectivity_file
from core.results_dir import (
    resolve_linker_platform_dir,
    resolve_linker_results_root,
    sanitize_project_name,
)

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

_LINKER_SRC_DIR = Path(__file__).resolve().parent
_LINKER_ROOT_DIR = _LINKER_SRC_DIR.parent
_LINKER_RESOURCES_DIR = _LINKER_ROOT_DIR / "resources"
_DEFAULT_INSTALL_DIR = _LINKER_ROOT_DIR / "results" / "base"

_DEFAULT_COMMON_PATHS: dict[str, Path] = {
    "bd_ports": _LINKER_RESOURCES_DIR / "bd_ports.txt",
    "template": _LINKER_RESOURCES_DIR / "slash.tcl",
    "service_template": _LINKER_RESOURCES_DIR / "service_layer.tcl",
    "sim_template": _LINKER_RESOURCES_DIR / "sim" / "sim_prj.tcl",
    "sim_mem": _LINKER_RESOURCES_DIR / "sim" / "sim_mem.v",
    "system_map_template": _LINKER_RESOURCES_DIR / "system_map.xml",
    "tb_template": _LINKER_RESOURCES_DIR / "sw_emu" / "tb.cpp",
}

_INTERNAL_COMMON_DEFAULTS: dict[str, str | None] = {
    "bd_ports": str(_DEFAULT_COMMON_PATHS["bd_ports"]),
    "template": str(_DEFAULT_COMMON_PATHS["template"]),
    "out": "slash.tcl",
    "service_template": str(_DEFAULT_COMMON_PATHS["service_template"]),
    "service_out": "service_layer_gen.tcl",
    "sim_template": str(_DEFAULT_COMMON_PATHS["sim_template"]),
    "sim_out": "run_pre.tcl",
    "sim_mem": str(_DEFAULT_COMMON_PATHS["sim_mem"]),
    "system_map_template": str(_DEFAULT_COMMON_PATHS["system_map_template"]),
    "system_map_out": "system_map.xml",
    "proj_root": None,
    "tb_template": str(_DEFAULT_COMMON_PATHS["tb_template"]),
    "tb_out": None,
    "emu_manifest_out": None,
    "clock_hz": None,
    "ip_repository": None,
}

_PATH_ARG_KEYS = {
    "cfg",
    "bd_ports",
    "template",
    "out",
    "service_template",
    "service_out",
    "sim_template",
    "sim_out",
    "sim_mem",
    "system_map_template",
    "system_map_out",
    "proj_root",
    "tb_template",
    "tb_out",
    "ip_repository",
    "emu_manifest_out",
}


def _abs_path(value: str, base_dir: Path) -> str:
    p = Path(value).expanduser()
    if not p.is_absolute():
        p = base_dir / p
    return str(p.resolve())


def _materialize_default_output_paths(args: argparse.Namespace) -> None:
    project = getattr(args, "project", None)
    if not project:
        return

    project_name = sanitize_project_name(project)
    project_results = resolve_linker_results_root(project_name)
    hw_results = resolve_linker_platform_dir(
        project_name, "hw", results_root=project_results)
    sim_results = resolve_linker_platform_dir(
        project_name, "sim", results_root=project_results)
    emu_results = resolve_linker_platform_dir(
        project_name, "emu", results_root=project_results)
    platform = getattr(args, "platform", None) or "hw"

    if getattr(args, "out", None) == "slash.tcl":
        args.out = str(hw_results / "bd" / f"slash_{project_name}.tcl")
    if getattr(args, "service_out", None) == "service_layer_gen.tcl":
        args.service_out = str(hw_results / "bd" /
                               f"service_layer_{project_name}.tcl")
    if getattr(args, "sim_out", None) == "run_pre.tcl":
        args.sim_out = str(sim_results / "run_pre.tcl")
    if getattr(args, "system_map_out", None) == "system_map.xml":
        if platform == "sim":
            args.system_map_out = str(sim_results / "system_map.xml")
        elif platform == "emu":
            args.system_map_out = str(emu_results / "system_map.xml")
        else:
            args.system_map_out = str(hw_results / "system_map.xml")

    if platform == "emu":
        if getattr(args, "tb_out", None) is None:
            args.tb_out = str(emu_results / "sw_emu" / "tb.cpp")
        if getattr(args, "emu_manifest_out", None) is None:
            args.emu_manifest_out = str(
                emu_results / "sw_emu" / "emu_manifest.json")


def _apply_internal_common_defaults(args: argparse.Namespace) -> None:
    for key, default in _INTERNAL_COMMON_DEFAULTS.items():
        if not hasattr(args, key) or getattr(args, key) is None:
            setattr(args, key, default)


def _stage_key(platform: Platform) -> str:
    if platform == Platform.SIMULATION:
        return "sim_stage"
    elif platform == Platform.EMULATION:
        return "emu_stage"
    else:
        return "hw_stage"


def _steps_for_platform(platform: Platform) -> tuple[str, ...]:
    if platform == Platform.SIMULATION:
        return SIM_STEPS
    elif platform == Platform.EMULATION:
        return EMU_STEPS
    else:
        return HW_STEPS


def _linker_info_path(project_name: str) -> Path:
    return resolve_linker_results_root(project_name) / ".linker_info.json"


def save_linker_info(config: LinkerConfiguration, stage: str) -> Path:
    steps = _steps_for_platform(config.platform)
    if stage not in steps:
        raise ValueError(
            f"Unknown stage '{stage}'. Expected one of: {', '.join(steps)}")

    out_path = config.linker_info_path
    args_payload = {k: v for k, v in vars(config.input_arguments).items() if k not in {
        "command", "func"}}

    payload: dict = {}
    if out_path.exists():
        with out_path.open("r", encoding="utf-8") as f:
            payload = json.load(f)

    # Merge args to avoid clobbering stored config when stage subcommands are used.
    merged_args = dict(payload.get("args", {}))
    for k, v in args_payload.items():
        if v is None and k in merged_args:
            continue
        merged_args[k] = str(v)
    payload["args"] = merged_args
    payload[_stage_key(config.platform)] = stage
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

def profiled(func) -> None:
    return lambda: run_with_profiling(func.__name__, func)

def run_with_profiling(label: str, func) -> None:
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
        raise ValueError(
            f"Unknown stage '{stage}'. Expected one of: {', '.join(steps)}")
    return steps.index(stage)


def _require_stage(payload: dict, required_stage: str, path: Path, platform: str) -> None:
    current = payload.get(_stage_key(platform))
    if current is None:
        raise ValueError(
            f"Missing stage for platform '{platform}' in linker info: {path}")
    if _stage_index(current, platform) < _stage_index(required_stage, platform):
        raise ValueError(
            f"Linker info {platform} stage '{current}' is before required stage "
            f"'{required_stage}' in {path}"
        )


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
    enabled_eth = getattr(net, "enabled_eth", set()
                          ) if net is not None else set()
    return bool(enabled_eth)


def _stage_init(args: argparse.Namespace) -> None:
    save_linker_info(args, stage="create_project")


def _stage_generate_tcl(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="create_project",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    linker_config = LinkerConfiguration(
        info_args.cfg,
        info_args.kernels,
        info_args.ip_repository,
        info_args.project,
        info_args.platform,
        shutil.which("vivado"), # Change to argument!
        8, # TODO: Change to argument!
        None
    )
    if info_args.platform == "sim":
        generate_sim_tcl(linker_config)
    elif info_args.platform == "emu":
        generate_emu_tcl(linker_config)
    else:
        generate_tcl(linker_config)
    save_linker_info(info_args, stage="generate_tcl", out_path=info_path)


def _stage_create_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="generate_tcl",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    linker_config = LinkerConfiguration(
        info_args.cfg,
        info_args.kernels,
        info_args.ip_repository,
        info_args.project,
        info_args.platform,
        shutil.which("vivado"), # Change to argument!
        8, # TODO: Change to argument!
        None
    )
    if info_args.platform == "sim":
        create_sim_project(linker_config)
    elif info_args.platform == "emu":
        pass
    else:
        create_build_project(linker_config)
    save_linker_info(info_args, stage="create_hw_project", out_path=info_path)


def _stage_build_hw_project(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="create_hw_project",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    linker_config = LinkerConfiguration(
        info_args.cfg,
        info_args.kernels,
        info_args.ip_repository,
        info_args.project,
        info_args.platform,
        shutil.which("vivado"), # TODO: Change to argument!
        8, # TODO: Change to argument!
        None
    )
    if info_args.platform == "sim":
        build_sim_project(linker_config)
    elif info_args.platform == "emu":
        build_emu_project(linker_config)
    else:
        create_build_project(linker_config, action="build")
        generate_base_pdi_with_aved(linker_config)
    save_linker_info(info_args, stage="build_hw_project", out_path=info_path)


def _stage_complete_hw_build(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    _require_stage(payload, required_stage="generate_tcl",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    save_linker_info(info_args, stage="build_hw_project", out_path=info_path)


def _build_service_layer_rm(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    if platform != "hw":
        raise ValueError(
            "build_service_layer_rm is only supported for --platform hw")
    _require_stage(payload, required_stage="generate_tcl",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    ip_repository = getattr(info_args, "ip_repository", None)
    if not ip_repository:
        raise ValueError(
            "Missing ip_repository in linker info; run init with --ip-repository")
    if not args.force and not _hw_has_enabled_eth(getattr(info_args, "cfg", None)):
        print(
            f"INFO: No eth_* enabled in cfg for project '{info_args.project}'. Skipping service_layer RM build.")
        return
    linker_config = LinkerConfiguration(
        info_args.cfg,
        info_args.kernels,
        info_args.ip_repository,
        info_args.project,
        info_args.platform,
        shutil.which("vivado"), # TODO: Change to argument!
        8, # TODO: Change to argument!
        None
    )
    build_service_layer_rm(linker_config)


def _build_slash_rm(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    if platform != "hw":
        raise ValueError("build_slash_rm is only supported for --platform hw")
    _require_stage(payload, required_stage="generate_tcl",
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    ip_repository = getattr(info_args, "ip_repository", None)
    if not ip_repository:
        raise ValueError("Missing ip_repository in linker info; run init with --ip-repository")
    cfg = parse_connectivity_file(info_args.cfg)
    user_region = getattr(cfg, "user_region", None)
    pre_synth_tcls = getattr(user_region, "pre_synth_tcls", []) if user_region is not None else []
    build_slash_rm(
        project_name=info_args.project,
        ip_repository=ip_repository,
        install_dir=Path(args.install_dir),
        vivado_bin=args.vivado_bin,
        workdir=Path(args.workdir) if args.workdir else None,
        jobs=args.jobs,
        linker_results_dir=resolve_linker_platform_dir(
            info_args.project, "hw", results_root=info_path.parent
        ),
        pre_synth_tcls=[Path(p) for p in pre_synth_tcls],
    )
    build_slash_rm(linker_config)


def _stage_create_metadata(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    required_stage = "build_hw_project"
    _require_stage(payload, required_stage=required_stage,
                   path=info_path, platform=platform)
    info_args = argparse.Namespace(**payload["args"])
    info_args.platform = platform
    _normalize_path_args(info_args, base_dir=_LINKER_SRC_DIR)
    linker_config = LinkerConfiguration(
        info_args.cfg,
        info_args.kernels,
        info_args.ip_repository,
        info_args.project,
        info_args.platform,
        shutil.which("vivado"), # TODO: Change to argument!
        8, # TODO: Change to argument!
        None
    )
    if info_args.platform == "sim":
        pass
    elif info_args.platform == "emu":
        package_emu_artifacts(linker_config)
    else:
        include_service_layer = _hw_has_enabled_eth(getattr(info_args, "cfg", None))
        hw_results_dir = resolve_linker_platform_dir(
            info_args.project, "hw", results_root=info_path.parent
        )
        generate_image(
            project_name=info_args.project,
            include_service_layer=include_service_layer,
            results_dir=hw_results_dir,
        )
        generate_util_report(project_name=info_args.project, results_dir=hw_results_dir)
        apply_timing_frequency_cap(
            project_name=info_args.project,
            system_map_path=hw_results_dir / "system_map.xml",
            base_freq_hz=400_000_000,
        )
        build_vbin(project_name=info_args.project, results_dir=hw_results_dir)
    save_linker_info(info_args, stage="create_metadata", out_path=info_path)


def _stage_clean(args: argparse.Namespace) -> None:
    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    info_path = info_path.resolve()
    if not info_path.exists():
        raise FileNotFoundError(
            f"Linker info not found: {info_path}. Run `init` first.")

    payload = _load_linker_info(info_path)
    payload["hw_stage"] = "create_project"
    payload["sim_stage"] = "create_project"
    payload["emu_stage"] = "create_project"
    payload["stage"] = "create_project"
    with info_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)

    project_dir = info_path.parent
    for child in project_dir.iterdir():
        if child.resolve() == info_path:
            continue
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()

    print(
        f"Cleaned linker artifacts under {project_dir}; preserved {info_path.name}.")


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

    info_path = Path(
        args.linker_info) if args.linker_info else _linker_info_path(args.project)
    payload = _load_linker_info(info_path)
    platform = _resolve_platform(payload, args)
    current_stage = payload.get(_stage_key(platform))
    steps = _steps_for_platform(platform)
    if current_stage not in steps:
        raise ValueError(
            f"Invalid or missing stage in linker info: {info_path}")

    current_idx = _stage_index(current_stage, platform)
    target_idx = _stage_index(target_stage, platform)

    rerun_target_only = False
    if current_idx >= target_idx:
        print(
            f"Project is already in {current_stage} state. Do you wish to rerun? Y/N.")
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
        run_with_profiling(stage, lambda f=func: f(args))

def link(config: LinkerConfiguration) -> None:
    run_with_profiling("create_project", lambda: save_linker_info(
        config, stage="create_project"))

    @profiled
    def step_generate_tcl() -> None:
        if config.platform == Platform.SIMULATION:
            generate_sim_tcl(config)
        elif config.platform == Platform.EMULATION:
            generate_emu_tcl(config)
        else:
            generate_tcl(config)
        save_linker_info(config, stage="generate_tcl")
    step_generate_tcl()

    @profiled
    def step_build_all() -> None:
        if config.platform == Platform.SIMULATION:
            create_sim_project(config)
            build_sim_project(config)
        elif config.platform == Platform.EMULATION:
            build_emu_project(config)
        else:
            create_build_project(config, action="all")
            generate_base_pdi_with_aved(config)
        save_linker_info(config, stage="build_hw_project")
    step_build_all()

    @profiled
    def step_create_metadata() -> None:
        if config.platform == Platform.SIMULATION:
            pass
        elif config.platform == Platform.EMULATION:
            package_emu_artifacts(config)
        else:
            generate_image(config)
            generate_util_report(config)
            build_vbin(config)
        save_linker_info(config, stage="create_metadata")
    step_create_metadata()

def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s:%(funcName)s: %(message)s",
    )

    ap = argparse.ArgumentParser(description="Todo", conflict_handler="resolve")
    sub_parsers = ap.add_subparsers(required=True)

    link_parser = sub_parsers.add_parser("link")
    LinkerConfiguration.populate_argument_parser(link_parser)
    link_parser.set_defaults(config_class=LinkerConfiguration, operation=link)
    
    install_parser = sub_parsers.add_parser("install")
    InstallerConfiguration.populate_argument_parser(install_parser)
    install_parser.set_defaults(config_class=InstallerConfiguration, operation=install_abstract_shell)
   
    args = ap.parse_args()

    config = args.config_class(args)
    args.operation(config)


if __name__ == "__main__":
    main()
