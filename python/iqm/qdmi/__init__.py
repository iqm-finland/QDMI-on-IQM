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
from .offloader import estimate, sample

__all__ = [
    "IQM_QDMI_CMAKE_DIR",
    "IQM_QDMI_INCLUDE_DIR",
    "IQM_QDMI_LIBRARY_PATH",
    "__version__",
    "estimate",
    "sample",
]


def __dir__() -> list[str]:
    return __all__


# If pickling is not supported by Qiskit's DataBin container, patch it.
# This shim is for compatibility with older Qiskit versions and can be removed once Qiskit >= 2.1.0 is required.
try:
    import pickle  # noqa: S403

    from qiskit.primitives.containers.data_bin import DataBin

    pickle.loads(pickle.dumps(DataBin()))  # noqa: S301
except ImportError:
    pass
except NotImplementedError:
    # Bypass the immutable __setattr__ restriction during unpickling
    DataBin.__setattr__ = object.__setattr__
