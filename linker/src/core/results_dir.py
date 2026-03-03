from __future__ import annotations

import os
from pathlib import Path

_RESULTS_ENV = "SLASH_LINKER_RESULTS_DIR"
_XDG_CACHE_ENV = "XDG_CACHE_HOME"
_HOME_ENV = "HOME"
_FALLBACK_CACHE_ROOT = Path("/home/user/.cache")


def resolve_linker_results_root() -> Path:
    """Resolve the linker results root using environment precedence."""
    if _RESULTS_ENV in os.environ:
        raw = os.environ.get(_RESULTS_ENV, "").strip()
        if raw == "":
            raise SystemExit(
                f"ERROR: {_RESULTS_ENV} is set but empty. Set it to an absolute directory path."
            )
        env_path = Path(raw).expanduser()
        if not env_path.is_absolute():
            raise SystemExit(
                f"ERROR: {_RESULTS_ENV} must be an absolute path, got: {raw!r}"
            )
        return env_path.resolve()

    xdg_cache_home = os.environ.get(_XDG_CACHE_ENV, "").strip()
    if xdg_cache_home:
        return (Path(xdg_cache_home).expanduser() / "slash" / "linker_results").resolve()

    home = os.environ.get(_HOME_ENV, "").strip()
    if home:
        return (Path(home).expanduser() / ".cache" / "slash" / "linker_results").resolve()

    return _FALLBACK_CACHE_ROOT / "slash" / "linker_results"
