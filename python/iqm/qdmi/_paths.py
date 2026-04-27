# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Resolved install paths for the packaged IQM QDMI device library."""

import sys
from importlib.metadata import distribution
from pathlib import Path


def _require_existing_path(path: Path, *, name: str) -> Path:
    """Return ``path`` if it exists, otherwise raise a file-not-found error.

    Raises:
        FileNotFoundError: If ``path`` does not exist.
    """
    if not path.exists():
        msg = f"{name} does not exist: {path}"
        raise FileNotFoundError(msg)
    return path


dist = distribution("iqm-qdmi")
located_include_dir = dist.locate_file("iqm/qdmi/data/include/iqm_qdmi")
resolved_include_dir = Path(str(located_include_dir)).resolve(strict=True)

IQM_QDMI_DATA = _require_existing_path(resolved_include_dir.parents[1], name="IQM_QDMI_DATA")


def _resolve_library_dir() -> Path:
    """Return the directory containing the packaged IQM QDMI shared library.

    Raises:
        FileNotFoundError: If the expected library directory does not exist.
    """
    if sys.platform == "win32":
        library_dir = IQM_QDMI_DATA / "bin"
        return _require_existing_path(library_dir, name="IQM_QDMI_LIBRARY_DIR")

    library_dir = IQM_QDMI_DATA / "lib"
    if library_dir.exists():
        return library_dir
    library_dir = IQM_QDMI_DATA / "lib64"
    if library_dir.exists():
        return library_dir
    msg = (
        f"Expected 'lib' or 'lib64' directory for IQM QDMI library on Unix-like systems, "
        f"but neither exists: {library_dir}"
    )
    raise FileNotFoundError(msg)


IQM_QDMI_LIBRARY_DIR = _require_existing_path(_resolve_library_dir(), name="IQM_QDMI_LIBRARY_DIR")

library_files = sorted(IQM_QDMI_LIBRARY_DIR.glob("*iqm-qdmi-device*"), key=lambda path: path.name)
if not library_files:
    msg = f"No IQM QDMI library found in: {IQM_QDMI_LIBRARY_DIR}"
    raise FileNotFoundError(msg)
if len(library_files) > 1:
    msg = (
        f"Multiple IQM QDMI libraries found in {IQM_QDMI_LIBRARY_DIR}: {', '.join(str(path) for path in library_files)}"
    )
    raise RuntimeError(msg)
IQM_QDMI_LIBRARY_PATH = library_files[0]

IQM_QDMI_INCLUDE_DIR = _require_existing_path(IQM_QDMI_DATA / "include", name="IQM_QDMI_INCLUDE_DIR")

IQM_QDMI_CMAKE_DIR = _require_existing_path(IQM_QDMI_DATA / "share" / "cmake", name="IQM_QDMI_CMAKE_DIR")

del dist, located_include_dir, resolved_include_dir
