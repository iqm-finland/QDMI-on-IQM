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

"""Python wrapper for exposing the IQM QDMI device library."""

from ._paths import IQM_QDMI_CMAKE_DIR, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_LIBRARY_PATH
from ._version import version as __version__
from .qiskit import IQMBackend

__all__ = ["IQM_QDMI_CMAKE_DIR", "IQM_QDMI_INCLUDE_DIR", "IQM_QDMI_LIBRARY_PATH", "IQMBackend", "__version__"]


def __dir__() -> list[str]:
    return __all__
