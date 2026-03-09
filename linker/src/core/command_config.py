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
import sys
from typing import Any, List, Optional, Union
import re
import os
import shutil
import argparse


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

class CommandConfiguration(object):
    @classmethod
    def populate_argument_parser(cls, ap: argparse.ArgumentParser):
        ap.formatter_class = argparse.RawTextHelpFormatter
        ap.add_argument("--ip-repository", required=False, type=Path, default=None, help="IP repository path (stored for linker stages).")
        ap.add_argument("--vivado", required=False, type=Path, default=None, help="Vivado binary to use for linking. If not given, it will be derived from PATH.")
        ap.add_argument("--jobs", required=False, type=int, default=8, help="Number of parallel jobs for Vivado runs.")

    def __init__(self, args: argparse.Namespace):
        self._args = args
        
        # Resolve and verify the IP repository
        if args.ip_repository is None:
            self._ip_repository: Optional[Path] = None
        else:
            self._ip_repository: Optional[Path] = Path(args.ip_repository).expanduser().resolve()
            if not self._ip_repository.is_dir():
                raise FileNotFoundError(self._ip_repository)
        
        # Resolve, if necessary find, and verify the Vivado binary
        self._vivado_bin: Path = args.vivado if args.vivado is not None else Path(shutil.which("vivado"))
        self._vivado_bin = self._vivado_bin.expanduser().resolve()
        if not self._vivado_bin.is_file():
            raise FileNotFoundError(self._vivado_bin)
        
        # Misc. arguments
        self._n_jobs: int = args.jobs
        
    @property
    def input_arguments(self) -> argparse.Namespace:
        return self._args

    @property
    def project_name(self) -> str:
        raise NotImplementedError()

    @property
    def linker_root_dir(self) -> Path:
        return self.linker_src_dir.parent.resolve()

    @property
    def linker_src_dir(self) -> Path:
        # Assumes that this class is defined in linker/src/core/linker_config.py!
        return Path(__file__).parent.parent.resolve()

    @property
    def build_dir(self) -> Path:
        raise NotImplementedError()

    @property
    def resources_dir(self) -> Path:
        return self.linker_root_dir / "resources"

    @property
    def install_dir(self) -> Path:
        return self.linker_root_dir / "results" / "base"

    @property
    def ip_repository(self) -> Optional[Path]:
        return self._ip_repository

    @property
    def vivado_bin(self) -> Path:
        return self._vivado_bin
    
    @property
    def n_jobs(self) -> int:
        return self._n_jobs


class LinkerConfiguration(CommandConfiguration):
    
    @classmethod
    def populate_argument_parser(cls, ap: argparse.ArgumentParser):
        super().populate_argument_parser(ap)
        ap.description = "Link kernel IP cores into a complete design and build a VBIN archive for emulation, simulation, or hardware execution." 
        ap.add_argument("-c", "--config", required=True, type=Path, help="Path to the connectivity configuration file (e.g. config.cfg).")
        ap.add_argument("-k", "--kernels", required=True, type=Path, nargs="+",  help="List of component.xml files to load as kernel IP cores.")
        ap.add_argument("-o", "--out", required=True, type=Path, help="Path to the final VBIN archive.")
        ap.add_argument("-p", "--platform", choices=["emu", "sim", "hw"], default="emu", help="Target platform (hw, sim, or emu). Default: emu")
        ap.add_argument("--pre-synth-tcls", type=Path, nargs="*", default=[], help="Paths to TCL scripts to run before synthesis (applies to hardware builds only).")
        ap.add_argument("--clock-hz", required=False, type=Optional[int], default=None, help="Target clock frequency in MHz.")
        

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)

        self._configuration_file = args.config.expanduser().resolve()
        if not self._configuration_file.is_file():
            raise FileNotFoundError(self._configuration_file)

        self._kernel_component_paths: List[Path] = [
            path.expanduser().resolve() for path in args.kernels]
        for kernel in self._kernel_component_paths:
            if not kernel.is_file():
                raise FileNotFoundError(kernel)
            
        self._out_path: Path = args.out.expanduser().resolve()
        if self._out_path.is_file():
            self._out_path.unlink()

        self._build_dir: Path = self._out_path.with_name(f"{self._out_path.name}.prj")
        if self._build_dir.is_dir():
            shutil.rmtree(self._build_dir)
        if self._build_dir.is_file():
            self._build_dir.unlink()
        self._build_dir.mkdir(parents=True)

        # Resolve and verify pre-synthesis TCLs (if any)
        self._pre_synth_tcls: List[Path] = []
        for path in args.pre_synth_tcls:
            path: Path = path.expanduser().resolve()
            if not path.is_file():
                raise FileNotFoundError(path)
            self._pre_synth_tcls.append(path)
        
        # Misc. arguments
        self._platform = Platform(args.platform)
        self._clock_hz: int = args.clock_hz

        # Sanitize the output file stem as the project name
        s2 = re.sub(r"[^A-Za-z0-9_]+", "_", str(self._out_path.stem).strip())
        if not s2:
            s2 = "proj"
        if s2[0].isdigit():
            s2 = "_" + s2
        self._project_name: str = s2

        self._vitis_include_dir = _find_vitis_include()

    @property
    def configuration_file(self) -> Path:
        return self._configuration_file
    
    @property
    def out_path(self) -> Path:
        return self._out_path

    @property
    def platform(self) -> Platform:
        return self._platform

    @property
    def project_name(self) -> str:
        return self._project_name

    @property
    def kernel_component_files(self) -> List[Path]:
        return self._kernel_component_paths

    @property
    def build_dir(self) -> Path:
        return self._build_dir

    @property
    def linker_info_path(self) -> Path:
        return self.build_dir / ".linker_info.json"

    @property
    def vitis_include_dir(self) -> Path:
        return self._vitis_include_dir
    
    @property
    def pre_synth_tcls(self) -> List[Path]:
        return self._pre_synth_tcls

    @property
    def clock_hz(self) -> Optional[int]:
        return self._clock_hz

class InstallerConfiguration(CommandConfiguration):
    @classmethod
    def populate_argument_parser(cls, ap: argparse.ArgumentParser):
        super().populate_argument_parser(ap)
        ap.description = "Build and install base images for hardware builds."
        ap.add_argument("--build-dir", required=False, type=Optional[Path], default=Path("./install.prj"), help="The build directory for the installer. Default: ./install_build")
    
    def __init__(self, args: argparse.Namespace):
        super().__init__(args)

        self._build_dir: Path = args.build_dir.expanduser().resolve()
        if self._build_dir.is_dir():
            shutil.rmtree(self._build_dir)
        self._build_dir.mkdir(parents=True)

    @property
    def project_name(self):
        return "slash_install"
    
    @property
    def build_dir(self):
        return self._build_dir

