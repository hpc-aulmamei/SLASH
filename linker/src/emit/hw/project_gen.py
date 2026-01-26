# project_gen.py
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
from typing import Optional


def _default_create_project_tcl() -> Path:
    # linker/src/emit/hw -> linker/resources/base/scripts/create_project.tcl
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "create_project.tcl"

def _default_pdi_dir() -> Path:
    # linker/src/emit/hw -> linker/results/<project>/images
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "build"

def _default_results_dir() -> Path:
    # linker/src/emit/hw -> linker/results
    return Path(__file__).resolve().parents[3]


def create_build_project(
    project_name: str,
    ip_repository: Optional[str] = None,
    tcl_path: Optional[Path] = None,
    vivado_bin: str = "vivado",
    workdir: Optional[Path] = None,
) -> None:
    tcl = Path(tcl_path) if tcl_path else _default_create_project_tcl()
    if not tcl.exists():
        raise FileNotFoundError(f"create_project.tcl not found: {tcl}")

    cmd = [vivado_bin, "-mode", "batch", "-source", str(tcl), "-tclargs", project_name]
    if ip_repository:
        cmd.append(ip_repository)

    subprocess.run(cmd, cwd=str(workdir) if workdir else None, check=True)


def generate_image(project_name: str, workdir: Optional[Path] = None) -> None:
    impl_dir = _default_pdi_dir() / "slash.runs" / f"{project_name}_impl_1"
    dest_dir = _default_results_dir() / "results" / project_name / "images"
    dest_dir.mkdir(parents=True, exist_ok=True)

    pdi_files = [
        f"top_i_service_layer_service_layer_{project_name}_inst_0_partial.pdi",
        f"top_i_slash_slash_{project_name}_inst_0_partial.pdi",
    ]

    for filename in pdi_files:
        src = impl_dir / filename
        if not src.exists():
            raise FileNotFoundError(f"Expected image file not found: {src}")
        shutil.copy2(src, dest_dir / filename)
