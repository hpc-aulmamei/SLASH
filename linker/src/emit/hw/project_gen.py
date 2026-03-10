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

from enum import Enum
import os
from pathlib import Path
import logging
import re
import shutil
import subprocess
from typing import Optional
from emit.metadata.report_util import convert_report_utilization_to_xml
from core.results_dir import resolve_linker_platform_dir
from core.command_config import LinkerConfiguration, InstallerConfiguration, CommandConfiguration

logger = logging.getLogger(__name__)

AVED_DESIGN_NAME = "amd_v80_gen5x8_25.1"


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
                    logger.info(
                        "Skipping copy because source and destination are the same file: %s", src)
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
    output_pdi = impl_dir / "top_wrapper.pdi"

    _ensure_boot_device_pcie_in_bif(bif_path)
    logger.info("Running bootgen in %s to generate %s",
                impl_dir, output_pdi.name)
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
        raise FileNotFoundError(
            f"Expected bootgen output not found: {output_pdi}")
    return output_pdi


def generate_base_pdi_with_aved(config: CommandConfiguration) -> Path:
    aved_reference_dir = config.resources_dir / "submodules" / "AVED"
    if not aved_reference_dir.is_dir():
        raise FileNotFoundError(aved_reference_dir)
    
    aved_dir = config.build_dir / "AVED"
    if aved_dir.is_dir():
        shutil.rmtree(aved_dir)
    shutil.copytree(aved_reference_dir, aved_dir)
    
    aved_hw_dir = aved_dir / "hw" / AVED_DESIGN_NAME
    aved_build_dir = aved_hw_dir / "build"
    aved_fpt_dir = aved_hw_dir / "fpt"
    aved_fw_profile_hal = aved_dir / "fw" / "AMC" / \
        "src" / "profiles" / "v80" / "profile_hal.h"

    static_impl_dir = config.build_dir / "slash.runs" / "impl_1"
    aved_build_script = config.resources_dir / "aved" / "build_all.sh"
    aved_profile_hal_src = config.resources_dir / "aved" / "profile_hal.h"
    aved_pdi_combine_src = config.resources_dir / "aved" / "pdi_combine.bif"
    xsa_src = config.resources_dir / "aved" / f"{AVED_DESIGN_NAME}.xsa"

    logger.info("Starting AVED base build for %s", config.project_name)
    aved_build_dir.mkdir(parents=True, exist_ok=True)

    regenerated_top_wrapper_pdi = _generate_top_wrapper_pdi_with_bootgen(
        static_impl_dir)
    _copy_checked(regenerated_top_wrapper_pdi,
                  aved_build_dir / "top_wrapper.pdi")
    _copy_checked(aved_build_script, aved_hw_dir / "build_all.sh")
    _copy_checked(aved_profile_hal_src, aved_fw_profile_hal)
    _copy_checked(aved_pdi_combine_src, aved_fpt_dir / "pdi_combine.bif")
    _copy_checked(xsa_src, aved_build_dir / f"{AVED_DESIGN_NAME}.xsa")

    logger.info("Running AVED build script in %s", aved_hw_dir)
    subprocess.run(["bash", "build_all.sh"], cwd=str(aved_hw_dir), check=True)

    aved_pdi = aved_hw_dir / f"{AVED_DESIGN_NAME}.pdi"
    if not aved_pdi.exists():
        raise FileNotFoundError(f"Expected AVED output not found: {aved_pdi}")
    logger.info("AVED fallback complete. Generated %s", aved_pdi)
    return aved_pdi


def create_build_project(
    config: CommandConfiguration,
    action: Optional[str] = None
) -> None:
    tcl = config.resources_dir / "base" / "scripts" / "create_project.tcl"
    if not tcl.exists():
        raise FileNotFoundError(f"create_project.tcl not found: {tcl}")

    log_path = config.build_dir / "vivado.log"

    cmd = [
        config.vivado_bin,
        "-mode",
        "batch",
        "-nojournal",
        "-log",
        str(log_path),
        "-source",
        str(tcl),
        "-tclargs",
        config.project_name,
    ]
    if config.ip_repository:
        cmd.append(str(config.ip_repository))
    if action:
        cmd.append(action)

    subprocess.run(cmd, cwd=str(config.build_dir), check=True)


class RM_KIND(Enum):
    SLASH_PROJECT = "slash"
    SERVICE_LAYER = "service_layer"


def _run_rm_build(config: LinkerConfiguration, rm_kind: RM_KIND) -> None:
    if not config.ip_repository:
        raise ValueError("ip_repository is required for RM builds")

    logs_dir = config.build_dir / "logs"
    artifact_out_dir = config.build_dir / \
        "slash.runs" / f"{config.project_name}_impl_1"
    rm_work_dir = config.build_dir / \
        "rm" / f"{rm_kind}_{config.project_name}"

    logs_dir.mkdir(parents=True, exist_ok=True)
    artifact_out_dir.mkdir(parents=True, exist_ok=True)
    rm_work_dir.mkdir(parents=True, exist_ok=True)

    if rm_kind == RM_KIND.SERVICE_LAYER:
        tcl_path = config.resources_dir / "base" / "scripts" / "service_layer_build.tcl"
        log_path = logs_dir / "service_layer_build.log"
    else:
        tcl_path = config.resources_dir / "base" / "scripts" / "slash_project_build.tcl"
        log_path = logs_dir / "slash_project_build.log"

    if not tcl_path.exists():
        raise FileNotFoundError(f"RM build Tcl not found: {tcl_path}")

    cmd = [
        config.vivado_bin,
        "-mode",
        "batch",
        "-nojournal",
        "-log",
        str(log_path),
        "-source",
        str(tcl_path),
        "-tclargs",
        "--project-name",
        config.project_name,
        "--ip-repo",
        str(config.ip_repository),
        "--install-dir",
        str(config.install_dir),
        "--linker-results-dir",
        str(config.build_dir),
        "--rm-work-dir",
        str(rm_work_dir),
        "--artifact-out-dir",
        str(artifact_out_dir),
        "--jobs",
        str(config.n_jobs),
    ]
    util_report_path = config.build_dir / "reports" / \
        f"report_utilization_{config.project_name}.txt"
    util_report_path.parent.mkdir(parents=True, exist_ok=True)
    cmd.extend(["--util-report-file", str(util_report_path)])

    if rm_kind == RM_KIND.SLASH_PROJECT:
        for path in config.pre_synth_tcls:
            cmd.extend(["--pre-synth-tcl", str(path)])

    subprocess.run(cmd, cwd=str(config.build_dir), check=True)


def build_service_layer_rm(config: LinkerConfiguration) -> None:
    _run_rm_build(config, RM_KIND.SERVICE_LAYER)


def build_slash_rm(config: LinkerConfiguration) -> None:
    _run_rm_build(config, RM_KIND.SLASH_PROJECT)


def install_abstract_shell(config: InstallerConfiguration) -> None:
    config.install_dir.mkdir(parents=True, exist_ok=True)

    create_build_project(config)

    impl_dir = config.build_dir / "slash.runs" / "impl_1"
    dcp_sources = (
        impl_dir / "top_wrapper_routed_bb.dcp",
        impl_dir / "abs_shell_slash.dcp",
        impl_dir / "abs_shell_service_layer.dcp",
    )
    for src in dcp_sources:
        if not src.exists():
            raise FileNotFoundError(
                f"Expected install artifact not found: {src}")
    _copy_files(list(dcp_sources), config.install_dir)

    src_dirs = config.build_dir / "slash.srcs" / "sources_1" / "bd"
    for src_dir in (src_dirs / "slash_base", src_dirs / "service_layer"):
        if not src_dir.is_dir():
            raise FileNotFoundError(
                f"Expected install BD directory not found: {src_dir}")
        _copy_tree(src_dir, config.install_dir)

    aved_pdi_path = generate_base_pdi_with_aved(config)
    if not aved_pdi_path.exists():
        raise FileNotFoundError(
            f"Expected AVED PDI not found in results/base: {aved_pdi_path}")
    _copy_files([aved_pdi_path], config.install_dir)


def generate_image(config: CommandConfiguration, include_service_layer: bool = True) -> None:
    impl_dir = config.build_dir / \
        "slash.runs" / f"{config.project_name}_impl_1"
    dest_dir = config.build_dir / "images"
    logger.info("Generating PDI images for project %s", config.project_name)
    logger.info("PDI source dir: %s", impl_dir)
    logger.info("PDI destination dir: %s", dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    service_layer_filename = f"top_i_service_layer_service_layer_{config.project_name}_inst_0_partial.pdi"
    if not include_service_layer:
        stale_service_pdi = dest_dir / service_layer_filename
        if stale_service_pdi.exists():
            logger.info("Removing stale service-layer PDI: %s",
                        stale_service_pdi)
            stale_service_pdi.unlink()

    pdi_files = []
    if include_service_layer:
        pdi_files.append(service_layer_filename)
    pdi_files.append(
        f"top_i_slash_slash_{config.project_name}_inst_0_partial.pdi")

    for filename in pdi_files:
        src = impl_dir / filename
        if not src.exists():
            raise FileNotFoundError(f"Expected image file not found: {src}")
        dest = dest_dir / filename
        logger.info("Copying PDI image: %s -> %s", src, dest)
        shutil.copy2(src, dest)
    logger.info("PDI image generation complete for %s", config.project_name)


def generate_util_report(config: CommandConfiguration) -> None:
    report_path = config.build_dir / \
        f"report_utilization_{config.project_name}.txt"
    xml_path = config.build_dir / \
        f"report_utilization_{config.project_name}.xml"
    logger.info("Generating utilization report XML for project %s",
                config.project_name)
    logger.info("Utilization report input: %s", report_path)
    logger.info("Utilization report output: %s", xml_path)
    if not report_path.exists():
        logger.warning(
            "Utilization report input missing for %s (%s). Skipping XML generation.",
            config.project_name,
            report_path,
        )
        return
    convert_report_utilization_to_xml(report_path, xml_path)
    logger.info("Utilization report XML generation complete for %s",
                config.project_name)
