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
import shutil
import subprocess
from typing import Optional
from emit.metadata.report_util import convert_report_utilization_to_xml

logger = logging.getLogger(__name__)

AVED_DESIGN_NAME = "amd_v80_gen5x8_25.1"


def _default_create_project_tcl() -> Path:
    # linker/src/emit/hw -> linker/resources/base/scripts/create_project.tcl
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "create_project.tcl"

def _default_clean_project_tcl() -> Path:
    # linker/src/emit/hw -> linker/resources/base/scripts/clean_project.tcl
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "clean_project.tcl"

def _default_pdi_dir() -> Path:
    # linker/src/emit/hw -> linker/results/<project>/images
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "build"

def _default_results_dir() -> Path:
    # linker/src/emit/hw -> linker/results
    return Path(__file__).resolve().parents[3]


def _copy_checked(src: Path, dest: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"Expected file not found: {src}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest)


def generate_base_pdi_with_aved(project_name: str, workdir: Optional[Path] = None) -> None:
    linker_root = _default_results_dir()
    results_base_dir = linker_root / "results" / "base"
    if results_base_dir.exists():
        logger.info("results/base already exists. Skipping AVED fallback build.")
        return

    aved_root = linker_root / "submodules" / "AVED"
    aved_hw_dir = aved_root / "hw" / AVED_DESIGN_NAME
    aved_build_dir = aved_hw_dir / "build"
    aved_fpt_dir = aved_hw_dir / "fpt"
    aved_fw_profile_hal = aved_root / "fw" / "AMC" / "src" / "profiles" / "v80" / "profile_hal.h"

    static_top_wrapper_pdi = (
        linker_root / "resources" / "base" / "build" / "slash.runs" / "impl_1" / "top_wrapper.pdi"
    )
    aved_build_script = linker_root / "resources" / "aved" / "build_all.sh"
    aved_profile_hal_src = linker_root / "resources" / "aved" / "profile_hal.h"
    aved_pdi_combine_src = linker_root / "resources" / "aved" / "pdi_combine.bif"
    xsa_src = linker_root / "results" / project_name / "top.xsa"

    logger.info("results/base not found. Starting AVED fallback build for %s", project_name)
    aved_build_dir.mkdir(parents=True, exist_ok=True)

    _copy_checked(static_top_wrapper_pdi, aved_build_dir / "top_wrapper.pdi")
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

    cmd = [vivado_bin, "-mode", "batch", "-source", str(tcl), "-tclargs", project_name]
    if ip_repository:
        cmd.append(ip_repository)
    if action:
        cmd.append(action)

    subprocess.run(cmd, cwd=str(workdir) if workdir else None, check=True)


def clean_hw_project(
    project_name: str,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    workdir: Optional[Path] = None,
) -> None:
    tcl = Path(tcl_path) if tcl_path else _default_clean_project_tcl()
    if not tcl.exists():
        raise FileNotFoundError(f"clean_project.tcl not found: {tcl}")

    cmd = [
        vivado_bin,
        "-mode",
        "batch",
        "-nolog",
        "-nojournal",
        "-source",
        str(tcl),
        "-tclargs",
        project_name,
    ]
    subprocess.run(cmd, cwd=str(workdir) if workdir else None, check=True)


def generate_image(project_name: str, workdir: Optional[Path] = None) -> None:
    impl_dir = _default_pdi_dir() / "slash.runs" / f"{project_name}_impl_1"
    dest_dir = _default_results_dir() / "results" / project_name / "images"
    logger.info("Generating PDI images for project %s", project_name)
    logger.info("PDI source dir: %s", impl_dir)
    logger.info("PDI destination dir: %s", dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    pdi_files = [
        f"top_i_service_layer_service_layer_{project_name}_inst_0_partial.pdi",
        f"top_i_slash_slash_{project_name}_inst_0_partial.pdi",
    ]

    for filename in pdi_files:
        src = impl_dir / filename
        if not src.exists():
            raise FileNotFoundError(f"Expected image file not found: {src}")
        dest = dest_dir / filename
        logger.info("Copying PDI image: %s -> %s", src, dest)
        shutil.copy2(src, dest)
    logger.info("PDI image generation complete for %s", project_name)

def generate_util_report(project_name: str) -> None:
    report_dir = _default_results_dir() / "results" / project_name

    report_file = report_dir / f"report_utilization_{project_name}.txt"
    xml_file = report_dir / f"report_utilization_{project_name}.xml"
    logger.info("Generating utilization report XML for project %s", project_name)
    logger.info("Utilization report input: %s", report_file)
    logger.info("Utilization report output: %s", xml_file)
    convert_report_utilization_to_xml(report_file, xml_file)
    logger.info("Utilization report XML generation complete for %s", project_name)
