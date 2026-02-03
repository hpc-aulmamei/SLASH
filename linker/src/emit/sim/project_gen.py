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
import tempfile
from typing import Iterable, Optional

logger = logging.getLogger(__name__)


def _results_root() -> Path:
    # linker/src/emit/sim -> linker/results
    return Path(__file__).resolve().parents[3] / "results"


def _repo_root() -> Path:
    # linker/src/emit/sim -> repo root
    return Path(__file__).resolve().parents[4]


def _sim_root(project_name: str) -> Path:
    return _results_root() / project_name / "sim"


def _sim_prj_dir(project_name: str) -> Path:
    return _sim_root(project_name) / "sim_prj"


def _copy_kernels_to_iprepo(component_xmls: Iterable[str | Path], iprepo_dir: Path) -> None:
    iprepo_dir.mkdir(parents=True, exist_ok=True)

    # Clean previous kernel_* entries
    for p in iprepo_dir.glob("kernel_*"):
        if p.is_dir():
            shutil.rmtree(p, ignore_errors=True)

    for kxml in component_xmls:
        kpath = Path(kxml).resolve()
        if not kpath.exists():
            raise FileNotFoundError(f"Kernel component.xml not found: {kpath}")
        kernel_dir = kpath.parent
        target_dir = Path(tempfile.mkdtemp(prefix="kernel_", dir=str(iprepo_dir)))
        shutil.copytree(kernel_dir, target_dir, dirs_exist_ok=True)


def create_sim_project(
    project_name: str,
    component_xmls: Iterable[str | Path],
    vivado_bin: str = "vivado",
    sim_tcl: Optional[Path] = None,
) -> None:
    sim_root = _sim_root(project_name)
    sim_root.mkdir(parents=True, exist_ok=True)
    # Clean generated subfolders but keep run_pre.tcl if already generated.
    for sub in ["sim_prj", "iprepo", "build", "xsim.dir"]:
        p = sim_root / sub
        if p.exists():
            shutil.rmtree(p, ignore_errors=True)
    for p in sim_root.glob("vpp_sim*"):
        if p.is_file():
            try:
                p.unlink()
            except OSError:
                pass

    iprepo_dir = sim_root / "iprepo"
    _copy_kernels_to_iprepo(component_xmls, iprepo_dir)

    tcl = Path(sim_tcl) if sim_tcl else sim_root / "run_pre.tcl"
    if not tcl.exists():
        raise FileNotFoundError(f"Simulation TCL not found: {tcl}")

    cmd = [vivado_bin, "-mode", "tcl", "-source", str(tcl)]
    subprocess.run(cmd, cwd=str(sim_root), check=True)


def build_sim_project(
    project_name: str,
    sim_src_dir: Optional[Path] = None,
) -> None:
    sim_root = _sim_root(project_name)
    sim_prj_dir = _sim_prj_dir(project_name)

    xsim_dir = sim_prj_dir / "sim_prj.sim" / "sim_1" / "behav" / "xsim"
    if not xsim_dir.exists():
        raise FileNotFoundError(f"XSIM dir not found: {xsim_dir}")

    subprocess.run(["./compile.sh"], cwd=str(xsim_dir), check=True)
    subprocess.run(["./elaborate.sh"], cwd=str(xsim_dir), check=True)

    build_dir = sim_root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    # Copy xsim.dir into build dir for sim executable
    xsim_build = build_dir / "xsim.dir"
    if xsim_build.exists():
        shutil.rmtree(xsim_build, ignore_errors=True)
    shutil.copytree(xsim_dir / "xsim.dir", xsim_build)

    if sim_src_dir is None:
        sim_src_dir = _repo_root() / "linker" / "sim"

    subprocess.run(["cmake", str(sim_src_dir)], cwd=str(build_dir), check=True)
    jobs = str(os.cpu_count() or 8)
    subprocess.run(["make", "-j", jobs], cwd=str(build_dir), check=True)

    vpp_sim = build_dir / "vpp_sim"
    if not vpp_sim.exists():
        raise FileNotFoundError(f"vpp_sim not found: {vpp_sim}")
    shutil.copy2(vpp_sim, sim_root / "vpp_sim")

    # Copy xsim.dir next to vpp_sim for runtime
    xsim_out = sim_root / "xsim.dir"
    if xsim_out.exists():
        shutil.rmtree(xsim_out, ignore_errors=True)
    shutil.copytree(xsim_build, xsim_out)

    system_map = sim_root / "system_map.xml"
    if not system_map.exists():
        raise FileNotFoundError(f"system_map.xml not found: {system_map}")

    # Package simulation artifacts into <project>_sim.vbin
    sim_vbin = sim_root / f"{project_name}_sim.vbin"
    if sim_vbin.exists():
        sim_vbin.unlink()
    with tarfile.open(sim_vbin, mode="w") as tf:
        tf.add(system_map, arcname="system_map.xml")
        tf.add(sim_root / "vpp_sim", arcname="vpp_sim")
        tf.add(sim_root / "xsim.dir", arcname="xsim.dir")

    logger.info("Simulation build outputs in %s", sim_root)
