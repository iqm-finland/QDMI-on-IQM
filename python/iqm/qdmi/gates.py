# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Qiskit gates for IQM device-native operations.

This module hosts gate classes that are not part of Qiskit's standard gate
library but are required to represent IQM device-native operations in a Qiskit
:class:`~qiskit.transpiler.Target`.
"""

from __future__ import annotations

try:
    from qiskit.circuit import Gate
except ImportError as e:
    msg = (
        "Failed to import Qiskit. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

__all__ = ["MoveGate"]


def __dir__() -> list[str]:
    return __all__


class MoveGate(Gate):
    """MOVE gate for IQM devices.

    The MOVE gate transfers the state of a qubit into a computational
    resonator (or back to the qubit), as used by IQM's star-topology
    architectures. The gate is intentionally kept opaque so Qiskit does not
    attempt to decompose it.
    """

    def __init__(self, label: str | None = None) -> None:
        """Initialize the MOVE gate.

        Args:
            label: Optional gate label.
        """
        super().__init__("move", 2, [], label=label)
