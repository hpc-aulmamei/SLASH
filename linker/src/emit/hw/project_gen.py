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

import os
from pathlib import Path
import logging
import re
import shutil
import subprocess
from typing import Optional
from emit.metadata.report_util import convert_report_utilization_to_xml
from core.results_dir import resolve_linker_results_root

logger = logging.getLogger(__name__)

AVED_DESIGN_NAME = "amd_v80_gen5x8_25.1"
HW_BUILD_DIR_ENV_KEYS = ("SLASH_HW_BUILD_DIR", "slash_hw_build_dir")


def _default_create_project_tcl() -> Path:
    # linker/src/emit/hw -> linker/resources/base/scripts/create_project.tcl
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "create_project.tcl"

def _default_slash_rm_build_tcl() -> Path:
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "slash_project_build.tcl"

def _default_service_layer_rm_build_tcl() -> Path:
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "service_layer_build.tcl"

def get_hw_build_dir() -> Path:
    for key in HW_BUILD_DIR_ENV_KEYS:
        configured_build_dir = os.getenv(key)
        if configured_build_dir:
            return Path(configured_build_dir).expanduser().resolve()

    raise SystemExit(
        "ERROR: Missing required HW build directory environment variable. "
        "Set SLASH_HW_BUILD_DIR (or slash_hw_build_dir) to an absolute writable path."
    )

def _default_pdi_dir() -> Path:
    return get_hw_build_dir()

def _default_results_dir() -> Path:
    # linker/src/emit/hw -> linker root
    return Path(__file__).resolve().parents[3]


def _default_project_results_root() -> Path:
    return resolve_linker_results_root()


def _default_install_dir() -> Path:
    return _default_results_dir() / "results" / "base"


def _copy_checked(src: Path, dest: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"Expected file not found: {src}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest)


def _copy_files(src_files: list[Path], destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for src in src_files:
        dst = destination / src.name
        # Allow install_dir to match the staging directory without failing on no-op copies.
        if dst.exists():
            try:
                if src.samefile(dst):
                    logger.info("Skipping copy because source and destination are the same file: %s", src)
                    continue
            except FileNotFoundError:
                pass
        shutil.copy2(src, dst)


def _copy_tree(src_dir: Path, destination: Path) -> None:
    target_dir = destination / src_dir.name
    target_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src_dir, target_dir, dirs_exist_ok=True)


def _ensure_boot_device_pcie_in_bif(bif_path: Path) -> None:
    if not bif_path.exists():
        raise FileNotFoundError(f"Expected BIF file not found: {bif_path}")

    lines = bif_path.read_text().splitlines()
    if any(line.strip() == "boot_device { pcie }" for line in lines):
        return

    # Find id=0x2
    pattern = re.compile(r"^(\s*)id\s*=\s*0x2\s*$")
    for idx, line in enumerate(lines):
        match = pattern.match(line)
        if match:
            lines.insert(idx + 1, f"{match.group(1)}boot_device {{ pcie }}")
            bif_path.write_text("\n".join(lines) + "\n")
            return

    raise ValueError(f"Could not find 'id = 0x2' in BIF file: {bif_path}")


def _generate_top_wrapper_pdi_with_bootgen(impl_dir: Path) -> Path:
    bif_path = impl_dir / "top_wrapper.bif"
    output_pdi = impl_dir / "top_wrapperr.pdi"

    _ensure_boot_device_pcie_in_bif(bif_path)
    logger.info("Running bootgen in %s to generate %s", impl_dir, output_pdi.name)
    subprocess.run(
        [
            "bootgen",
            "-arch",
            "versal",
            "-image",
            bif_path.name,
            "-w",
            "-o",
            output_pdi.name,
        ],
        cwd=str(impl_dir),
        check=True,
    )

    if not output_pdi.exists():
        raise FileNotFoundError(f"Expected bootgen output not found: {output_pdi}")
    return output_pdi


def generate_base_pdi_with_aved(project_name: str, workdir: Optional[Path] = None) -> None:
    linker_root = _default_results_dir()
    results_base_dir = linker_root / "results" / "base"
    aved_root = linker_root / "submodules" / "AVED"
    aved_hw_dir = aved_root / "hw" / AVED_DESIGN_NAME
    aved_build_dir = aved_hw_dir / "build"
    aved_fpt_dir = aved_hw_dir / "fpt"
    aved_fw_profile_hal = aved_root / "fw" / "AMC" / "src" / "profiles" / "v80" / "profile_hal.h"

    static_impl_dir = get_hw_build_dir() / "slash.runs" / "impl_1"
    aved_build_script = linker_root / "resources" / "aved" / "build_all.sh"
    aved_profile_hal_src = linker_root / "resources" / "aved" / "profile_hal.h"
    aved_pdi_combine_src = linker_root / "resources" / "aved" / "pdi_combine.bif"
    xsa_src = linker_root / "resources" / "aved" / f"{AVED_DESIGN_NAME}.xsa"

    logger.info("Starting AVED base build for %s", project_name)
    aved_build_dir.mkdir(parents=True, exist_ok=True)

    regenerated_top_wrapper_pdi = _generate_top_wrapper_pdi_with_bootgen(static_impl_dir)
    _copy_checked(regenerated_top_wrapper_pdi, aved_build_dir / "top_wrapper.pdi")
    _copy_checked(aved_build_script, aved_hw_dir / "build_all.sh")
    _copy_checked(aved_profile_hal_src, aved_fw_profile_hal)
    _copy_checked(aved_pdi_combine_src, aved_fpt_dir / "pdi_combine.bif")
    _copy_checked(xsa_src, aved_build_dir / f"{AVED_DESIGN_NAME}.xsa")

    logger.info("Running AVED build script in %s", aved_hw_dir)
    subprocess.run(["bash", "build_all.sh"], cwd=str(aved_hw_dir), check=True)

    aved_pdi = aved_hw_dir / f"{AVED_DESIGN_NAME}.pdi"
    if not aved_pdi.exists():
        raise FileNotFoundError(f"Expected AVED output not found: {aved_pdi}")
    results_base_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(aved_pdi, results_base_dir / f"{AVED_DESIGN_NAME}.pdi")
    logger.info("AVED fallback complete. Generated %s", results_base_dir / f"{AVED_DESIGN_NAME}.pdi")


def create_build_project(
    project_name: str,
    ip_repository: Optional[str] = None,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    workdir: Optional[Path] = None,
    action: Optional[str] = None,
) -> None:
    tcl = Path(tcl_path) if tcl_path else _default_create_project_tcl()
    if not tcl.exists():
        raise FileNotFoundError(f"create_project.tcl not found: {tcl}")

    log_path = _default_project_results_root() / project_name / "vivado.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        vivado_bin,
        "-mode",
        "batch",
        "-nojournal",
        "-log",
        str(log_path),
        "-source",
        str(tcl),
        "-tclargs",
        project_name,
    ]
    if ip_repository:
        cmd.append(ip_repository)
    if action:
        cmd.append(action)

    subprocess.run(cmd, cwd=str(workdir) if workdir else None, check=True)


def _run_rm_build(
    *,
    project_name: str,
    ip_repository: str,
    rm_kind: str,
    tcl_path: Path,
    vivado_bin: str = "vivado",
    install_dir: Optional[Path] = None,
    workdir: Optional[Path] = None,
    jobs: int = 8,
    util_report_file: Optional[Path] = None,
) -> None:
    if not ip_repository:
        raise ValueError("ip_repository is required for RM builds")

    tcl = Path(tcl_path)
    if not tcl.exists():
        raise FileNotFoundError(f"RM build Tcl not found: {tcl}")

    linker_results_dir = _default_project_results_root()
    hw_build_dir = get_hw_build_dir()
    logs_dir = hw_build_dir / "logs"
    artifact_out_dir = hw_build_dir / "slash.runs" / f"{project_name}_impl_1"
    rm_work_dir = hw_build_dir / "rm" / f"{rm_kind}_{project_name}"
    install_root = (install_dir if install_dir else _default_install_dir()).expanduser().resolve()

    logs_dir.mkdir(parents=True, exist_ok=True)
    artifact_out_dir.mkdir(parents=True, exist_ok=True)
    rm_work_dir.mkdir(parents=True, exist_ok=True)

    log_name = "service_layer_build.log" if rm_kind == "service_layer" else "slash_project_build.log"
    log_path = logs_dir / log_name

    cmd = [
        vivado_bin,
        "-mode",
        "batch",
        "-nojournal",
        "-log",
        str(log_path),
        "-source",
        str(tcl),
        "-tclargs",
        "--project-name",
        project_name,
        "--ip-repo",
        str(Path(ip_repository).expanduser().resolve()),
        "--install-dir",
        str(install_root),
        "--linker-results-dir",
        str(linker_results_dir),
        "--rm-work-dir",
        str(rm_work_dir),
        "--artifact-out-dir",
        str(artifact_out_dir),
        "--jobs",
        str(jobs),
    ]
    if util_report_file is not None:
        util_report_path = util_report_file.expanduser().resolve()
        util_report_path.parent.mkdir(parents=True, exist_ok=True)
        cmd.extend(["--util-report-file", str(util_report_path)])

    subprocess.run(cmd, cwd=str(workdir) if workdir else str(hw_build_dir), check=True)


def build_service_layer_rm(
    project_name: str,
    ip_repository: str,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    install_dir: Optional[Path] = None,
    workdir: Optional[Path] = None,
    jobs: int = 8,
) -> None:
    _run_rm_build(
        project_name=project_name,
        ip_repository=ip_repository,
        rm_kind="service_layer",
        tcl_path=Path(tcl_path) if tcl_path else _default_service_layer_rm_build_tcl(),
        vivado_bin=vivado_bin,
        install_dir=install_dir,
        workdir=workdir,
        jobs=jobs,
    )


def build_slash_rm(
    project_name: str,
    ip_repository: str,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    install_dir: Optional[Path] = None,
    workdir: Optional[Path] = None,
    jobs: int = 8,
) -> None:
    hw_build_dir = get_hw_build_dir()
    util_report_file = hw_build_dir / "reports" / f"report_utilization_{project_name}.txt"
    _run_rm_build(
        project_name=project_name,
        ip_repository=ip_repository,
        rm_kind="slash",
        tcl_path=Path(tcl_path) if tcl_path else _default_slash_rm_build_tcl(),
        vivado_bin=vivado_bin,
        install_dir=install_dir,
        workdir=workdir,
        jobs=jobs,
        util_report_file=util_report_file,
    )


def install_abstract_shell(
    project_name: str,
    ip_repository: Optional[str] = None,
    install_dir: Optional[Path] = None,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    workdir: Optional[Path] = None,
) -> None:
    # Install flow requires an explicit HW build root.
    get_hw_build_dir()

    # `build_project.tcl` writes abstract-shell DCPs into <linker>/results/base.
    # Ensure it exists before Vivado runs to avoid write_abstract_shell failures.
    (_default_results_dir() / "results" / "base").mkdir(parents=True, exist_ok=True)

    create_build_project(
        project_name=project_name,
        ip_repository=ip_repository,
        tcl_path=tcl_path,
        vivado_bin=vivado_bin,
        workdir=workdir,
    )

    impl_dir = get_hw_build_dir() / "slash.runs" / "impl_1"
    bd_source_dirs = (
        get_hw_build_dir() / "slash.srcs" / "sources_1" / "bd" / "slash_base",
        get_hw_build_dir() / "slash.srcs" / "sources_1" / "bd" / "service_layer",
    )
    results_base_dir = _default_results_dir() / "results" / "base"
    dcp_sources = (
        impl_dir / "top_wrapper_routed_bb.dcp",
        results_base_dir / "abs_shell_slash.dcp",
        results_base_dir / "abs_shell_service_layer.dcp",
    )
    destination = (install_dir if install_dir else _default_install_dir()).expanduser().resolve()
    destination.mkdir(parents=True, exist_ok=True)

    for src in dcp_sources:
        if not src.exists():
            raise FileNotFoundError(f"Expected install artifact not found: {src}")
    _copy_files(list(dcp_sources), destination)

    for src_dir in bd_source_dirs:
        if not src_dir.is_dir():
            raise FileNotFoundError(f"Expected install BD directory not found: {src_dir}")
        _copy_tree(src_dir, destination)

    generate_base_pdi_with_aved(project_name=project_name, workdir=workdir)
    aved_pdi = results_base_dir / f"{AVED_DESIGN_NAME}.pdi"
    if not aved_pdi.exists():
        raise FileNotFoundError(f"Expected AVED PDI not found in results/base: {aved_pdi}")
    _copy_files([aved_pdi], destination)

def generate_image(
    project_name: str,
    workdir: Optional[Path] = None,
    include_service_layer: bool = True,
) -> None:
    impl_dir = _default_pdi_dir() / "slash.runs" / f"{project_name}_impl_1"
    dest_dir = _default_project_results_root() / project_name / "images"
    logger.info("Generating PDI images for project %s", project_name)
    logger.info("PDI source dir: %s", impl_dir)
    logger.info("PDI destination dir: %s", dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    service_layer_filename = f"top_i_service_layer_service_layer_{project_name}_inst_0_partial.pdi"
    if not include_service_layer:
        stale_service_pdi = dest_dir / service_layer_filename
        if stale_service_pdi.exists():
            logger.info("Removing stale service-layer PDI: %s", stale_service_pdi)
            stale_service_pdi.unlink()

    pdi_files = []
    if include_service_layer:
        pdi_files.append(service_layer_filename)
    pdi_files.append(f"top_i_slash_slash_{project_name}_inst_0_partial.pdi")

    for filename in pdi_files:
        src = impl_dir / filename
        if not src.exists():
            raise FileNotFoundError(f"Expected image file not found: {src}")
        dest = dest_dir / filename
        logger.info("Copying PDI image: %s -> %s", src, dest)
        shutil.copy2(src, dest)
    logger.info("PDI image generation complete for %s", project_name)

def generate_util_report(project_name: str) -> None:
    report_dir = _default_project_results_root() / project_name

    report_file = report_dir / f"report_utilization_{project_name}.txt"
    xml_file = report_dir / f"report_utilization_{project_name}.xml"
    logger.info("Generating utilization report XML for project %s", project_name)
    logger.info("Utilization report input: %s", report_file)
    logger.info("Utilization report output: %s", xml_file)
    if not report_file.exists():
        logger.warning(
            "Utilization report input missing for %s (%s). Skipping XML generation.",
            project_name,
            report_file,
        )
        return
    convert_report_utilization_to_xml(report_file, xml_file)
    logger.info("Utilization report XML generation complete for %s", project_name)
