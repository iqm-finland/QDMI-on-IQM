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

from importlib.metadata import distribution
from pathlib import Path

dist = distribution("iqm-qdmi")
located_include_dir = dist.locate_file("iqm/qdmi/data/include/iqm_qdmi")
resolved_include_dir = Path(str(located_include_dir)).resolve(strict=True)

IQM_QDMI_DATA = resolved_include_dir.parents[1]
assert IQM_QDMI_DATA.exists(), f"IQM_QDMI_DATA does not exist: {IQM_QDMI_DATA}"


def _resolve_library_dir() -> Path:
    """Return the directory containing the packaged IQM QDMI shared library."""
    library_dir = IQM_QDMI_DATA / "lib"
    if library_dir.exists():
        return library_dir
    return IQM_QDMI_DATA / "lib64"


IQM_QDMI_LIBRARY_DIR = _resolve_library_dir()
assert IQM_QDMI_LIBRARY_DIR.exists(), f"IQM_QDMI_LIBRARY_DIR does not exist: {IQM_QDMI_LIBRARY_DIR}"

library_files = list(IQM_QDMI_LIBRARY_DIR.glob("*iqm-qdmi-device*"))
if not library_files:
    msg = f"No IQM QDMI library found in: {IQM_QDMI_LIBRARY_DIR}"
    raise FileNotFoundError(msg)
IQM_QDMI_LIBRARY_PATH = library_files[0]

IQM_QDMI_INCLUDE_DIR = IQM_QDMI_DATA / "include"
assert IQM_QDMI_INCLUDE_DIR.exists(), f"IQM_QDMI_INCLUDE_DIR does not exist: {IQM_QDMI_INCLUDE_DIR}"

IQM_QDMI_CMAKE_DIR = IQM_QDMI_DATA / "share" / "cmake"
assert IQM_QDMI_CMAKE_DIR.exists(), f"IQM_QDMI_CMAKE_DIR does not exist: {IQM_QDMI_CMAKE_DIR}"

del dist, located_include_dir, resolved_include_dir
