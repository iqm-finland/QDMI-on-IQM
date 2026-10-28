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

"""Hermetic compiler targets for provider adapter tests."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest
from mqt.core.mlir import (
    CompiledProgram,
    CompilerTarget,
    serialize_program,
)

from iqm.qdmi import serializers

if TYPE_CHECKING:
    from mqt.core.mlir import TerminalReadout
    from mqt.core.qdmi import Device, ProgramFormat
    from qiskit.circuit import QuantumCircuit


@pytest.fixture
def serialization_target(monkeypatch: pytest.MonkeyPatch) -> None:
    """Use actual native serialization with the tests' synthetic device sites."""

    def serialize(
        circuit: QuantumCircuit,
        *,
        target: Device,
        program_format: ProgramFormat,
        terminal_readout: TerminalReadout,
    ) -> CompiledProgram:
        snapshot = CompilerTarget(
            [CompilerTarget.Site(index, site.name()) for index, site in enumerate(target.sites())],
            connectivity=CompilerTarget.Connectivity.all_to_all(),
            native_operations=CompilerTarget.NativeOperations.unrestricted(),
        )
        return serialize_program(
            circuit, target=snapshot, program_format=program_format, terminal_readout=terminal_readout
        )

    monkeypatch.setattr(serializers, "serialize_program", serialize)
