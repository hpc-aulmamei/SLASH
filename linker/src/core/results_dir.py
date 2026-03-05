from __future__ import annotations

import re
from pathlib import Path

_PROJECT_RESULTS_PREFIX = "linker_results_"
_PLATFORMS = {"hw", "sim", "emu"}


def sanitize_project_name(name: str) -> str:
    """Sanitize a project name for filesystem-safe linker result directories."""
    s2 = re.sub(r"[^A-Za-z0-9_]+", "_", str(name).strip())
    if not s2:
        s2 = "proj"
    if s2[0].isdigit():
        s2 = "_" + s2
    return s2


def resolve_linker_results_root(project_name: str | None = None) -> Path:
    """Resolve linker results to local directories in the current working directory.

    - If project_name is provided: <cwd>/linker_results_<project>
    - Otherwise: <cwd>/linker_results
    """
    cwd = Path.cwd().resolve()
    if project_name is None:
        return cwd / "linker_results"
    return cwd / f"{_PROJECT_RESULTS_PREFIX}{sanitize_project_name(project_name)}"


def resolve_linker_platform_dir(
    project_name: str,
    platform: str,
    *,
    results_root: Path | None = None,
) -> Path:
    """Resolve a platform-specific results directory for a project.

    The returned path is:
      <results_root>/<platform>
    where results_root defaults to resolve_linker_results_root(project_name).
    """
    plat = str(platform).strip().lower()
    if plat not in _PLATFORMS:
        raise ValueError(
            f"Unsupported platform '{platform}'. Expected one of: {', '.join(sorted(_PLATFORMS))}"
        )
    root = Path(results_root).resolve() if results_root is not None else resolve_linker_results_root(project_name)
    return root / plat
