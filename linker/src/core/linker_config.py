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
from enum import Enum
from pathlib import Path
from typing import Any, List, Optional, Union
import re
import os
import shutil


class Platform(Enum):
    HARDWARE = "hw"
    SIMULATION = "sim"
    EMULATION = "emu"


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


class LinkerConfiguration(object):
    def __init__(
        self,
        configuration_file: Union[str, Path],
        kernel_component_files: List[Union[str, Path]],
        ip_repository: Optional[Union[str, Path]],
        project_name: str,
        platform: Union[str, Platform],
        vivado_bin: Union[str, Path],
        n_jobs: int,
        clock_hz: Optional[int]
    ):
        self._configuration_file: Path = Path(configuration_file).expanduser().resolve()
        self._kernel_component_files: List[Path] = [
            Path(path).expanduser().resolve() for path in kernel_component_files]
        self._ip_repository: Optional[Path] = Path(
            ip_repository).expanduser().resolve() if ip_repository is not None else None

        # Sanitize the project name
        s2 = re.sub(r"[^A-Za-z0-9_]+", "_", str(project_name).strip())
        if not s2:
            s2 = "proj"
        if s2[0].isdigit():
            s2 = "_" + s2
        self._project_name = s2

        self._platform = Platform(platform)
        self._vitis_include_dir = _find_vitis_include()
        self._vivado_bin = Path(vivado_bin).expanduser().resolve()
        self._n_jobs = n_jobs
        self._clock_hz = int(clock_hz) if clock_hz is not None else None

        # TODO: Turn this into a CLI argument or derive it from other arguments!
        hw_build_dir = next(os.getenv(key) for key in ("SLASH_HW_BUILD_DIR", "slash_hw_build_dir"))
        if hw_build_dir is None:
            raise SystemExit(
        "ERROR: Missing required HW build directory environment variable. "
        "Set SLASH_HW_BUILD_DIR (or slash_hw_build_dir) to an absolute writable path.")
        self._hardware_build_dir = Path(hw_build_dir).expanduser().resolve()


    @property
    def configuration_file(self) -> Path:
        return self._configuration_file

    @property
    def platform(self) -> Platform:
        return self._platform

    @property
    def project_name(self) -> str:
        return self._project_name

    @property
    def kernel_component_files(self) -> List[Path]:
        return self._kernel_component_files

    @property
    def linker_root_dir(self) -> Path:
        return self.linker_src_dir.parent.resolve()

    @property
    def linker_src_dir(self) -> Path:
        return Path(__file__).parent.parent.resolve()

    @property
    def results_dir(self) -> Path:
        # TODO: Change to not work from CWD
        return Path(os.getcwd()) / f"linker_results_{self.project_name}"

    @property
    def linker_info_path(self) -> Path:
        return self.results_dir / ".linker_info.json"

    @property
    def platform_results_dir(self) -> Path:
        return self.results_dir / str(self.platform.value)
    
    @property
    def hardware_build_dir(self) -> Path:
        return self._hardware_build_dir

    @property
    def resources_dir(self) -> Path:
        return self.linker_root_dir / "resources"

    @property
    def install_dir(self) -> Path:
        return self.linker_root_dir / "results" / "base"

    @property
    def vitis_include_dir(self) -> Path:
        return self._vitis_include_dir

    @property
    def ip_repository(self) -> Optional[Path]:
        return self._ip_repository

    @property
    def vivado_bin(self) -> Path:
        return self._vivado_bin
    
    @property
    def n_jobs(self) -> int:
        return self._n_jobs

    @property
    def clock_hz(self) -> Optional[int]:
        return self._clock_hz
