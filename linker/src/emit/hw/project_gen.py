# project_gen.py
from __future__ import annotations

from pathlib import Path
import subprocess
from typing import Optional


def _default_create_project_tcl() -> Path:
    # linker/src/emit/hw -> linker/resources/base/scripts/create_project.tcl
    return Path(__file__).resolve().parents[3] / "resources" / "base" / "scripts" / "create_project.tcl"


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
