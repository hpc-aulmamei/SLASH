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
import os
import shutil
import subprocess
import tarfile
from typing import Iterable, Optional

logger = logging.getLogger(__name__)


def _results_root() -> Path:
    # linker/src/emit/emu -> linker/results
    return Path(__file__).resolve().parents[3] / "results"


def _emu_root(project_name: str) -> Path:
    return _results_root() / project_name / "sw_emu"


def _project_root(project_name: str) -> Path:
    return _results_root() / project_name


def _find_vitis_include() -> Path:
    env_candidates = [
        os.environ.get("XILINX_VITIS"),
        os.environ.get("VITIS_HOME"),
        os.environ.get("VITIS"),
    ]
    for base in env_candidates:
        if not base:
            continue
        cand = Path(base) / "include"
        if cand.exists():
            return cand

    vitis_bin = shutil.which("vitis")
    if vitis_bin:
        return Path(vitis_bin).resolve().parents[1] / "include"

    raise FileNotFoundError(
        "Could not locate Vitis include path. Set XILINX_VITIS/VITIS_HOME "
        "or ensure 'vitis' is on PATH."
    )


def _collect_kernel_cpp(component_xmls: Iterable[str | Path]) -> list[Path]:
    cpp_files: list[Path] = []
    seen: set[Path] = set()

    for kxml in component_xmls:
        kpath = Path(kxml).resolve()
        if not kpath.exists():
            raise FileNotFoundError(f"Kernel component.xml not found: {kpath}")
        # component.xml -> ip -> impl -> <solution>
        sol_dir = kpath.parents[2]
        build_dir = sol_dir.parent

        # Prefer the original kernel sources in the build dir (e.g., build_increment.../*.cpp).
        candidates = list(build_dir.glob("*.cpp"))
        if not candidates:
            # Fallbacks for other layouts.
            candidates = list(sol_dir.glob("*.cpp"))
        if not candidates:
            candidates = list((kpath.parent.parent).glob("*.cpp"))

        for cpp in sorted(candidates):
            if cpp not in seen:
                seen.add(cpp)
                cpp_files.append(cpp)

    return cpp_files


def build_emu_project(
    project_name: str,
    component_xmls: Iterable[str | Path],
    *,
    tb_cpp: Optional[Path] = None,
    output_name: str = "vpp_emu",
) -> None:
    emu_root = _emu_root(project_name)
    emu_root.mkdir(parents=True, exist_ok=True)

    tb_path = Path(tb_cpp) if tb_cpp else emu_root / "tb.cpp"
    if not tb_path.exists():
        raise FileNotFoundError(f"tb.cpp not found: {tb_path}")

    cpp_files = [tb_path] + _collect_kernel_cpp(component_xmls)
    if not cpp_files:
        raise FileNotFoundError("No C++ sources found to build emulation executable.")

    vitis_include = _find_vitis_include()
    out_path = emu_root / output_name

    cmd = (
        ["g++"]
        + [str(p) for p in cpp_files]
        + ["-o", str(out_path), "-I", str(vitis_include), "-lzmq", "-I", "/usr/include/jsoncpp/", "-ljsoncpp"]
    )
    logger.info("Building emulation executable: %s", " ".join(cmd))
    subprocess.run(cmd, cwd=str(emu_root), check=True)
    logger.info("Emulation build outputs in %s", emu_root)


def package_emu_artifacts(
    project_name: str,
    *,
    output_name: Optional[str] = None,
) -> Path:
    project_root = _project_root(project_name)
    emu_root = _emu_root(project_name)
    system_map = project_root / "system_map.xml"
    vpp_emu = emu_root / "vpp_emu"

    if not system_map.exists():
        raise FileNotFoundError(f"system_map.xml not found: {system_map}")
    if not vpp_emu.exists():
        raise FileNotFoundError(f"vpp_emu not found: {vpp_emu}")

    out_name = output_name or f"{project_name}_emu.vbin"
    out_path = project_root / out_name
    if out_path.exists():
        out_path.unlink()

    with tarfile.open(out_path, mode="w") as tf:
        tf.add(system_map, arcname="system_map.xml")
        tf.add(vpp_emu, arcname="vpp_emu")

    logger.info("Emulation vbin in %s", out_path)
    return out_path
