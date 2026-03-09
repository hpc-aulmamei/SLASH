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

import logging
import tarfile
from pathlib import Path
from typing import Iterable, Optional

from core.results_dir import resolve_linker_platform_dir
from core.linker_config import LinkerConfiguration

logger = logging.getLogger(__name__)


def _default_results_root(project_name: str) -> Path:
    return resolve_linker_platform_dir(project_name, "hw")


def _iter_files(paths: Iterable[Path]) -> Iterable[Path]:
    for p in paths:
        if p.is_file():
            yield p


def build_vbin(config: LinkerConfiguration) -> Path:
    """! @brief Build a compressed .vbin tarball for a project.

    @param project_name Project name used to locate results and name the archive.
    @param results_dir Optional override of the project results directory.
    @return Path to the generated .vbin file.
    """
    project_dir = config.build_dir
    images_dir = project_dir / "images"
    util_xml = project_dir / f"report_utilization_{config.project_name}.xml"
    system_map = project_dir / "system_map.xml"
    out_path = config.out_path

    if not project_dir.exists():
        raise FileNotFoundError(f"Project results directory not found: {project_dir}")
    if not images_dir.exists():
        raise FileNotFoundError(f"Images directory not found: {images_dir}")
    if not util_xml.exists():
        raise FileNotFoundError(f"Utilization XML not found: {util_xml}")
    if not system_map.exists():
        raise FileNotFoundError(f"System map not found: {system_map}")

    logger.info("Creating vbin archive: %s", config.out_path)

    files_to_add = []
    files_to_add.extend(sorted(_iter_files(images_dir.rglob("*"))))
    files_to_add.append(util_xml)
    files_to_add.append(system_map)

    with tarfile.open(config.out_path, "w:gz") as tf:
        for path in files_to_add:
            arcname = path.relative_to(project_dir)
            logger.info("Adding to vbin: %s", arcname)
            tf.add(path, arcname=str(arcname))

    logger.info("vbin archive complete: %s", config.out_path)
    return config.out_path
