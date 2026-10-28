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
from unittest.mock import Mock

import pytest
from mqt.core.mlir import (
    CompiledProgram,
    CompilerTarget,
    _operation_parameter_constraints,  # ruff: ignore[import-private-name] Native bridge under test.
    serialize_program,
)
from mqt.core.plugins.qiskit import backend as qdmi_backend

from iqm.qdmi import serializers

if TYPE_CHECKING:
    from mqt.core.qdmi import Device, ProgramFormat
    from qiskit.circuit import QuantumCircuit


@pytest.fixture
def operation_constraints(monkeypatch: pytest.MonkeyPatch) -> None:
    """Keep fake operation metadata local while querying real native handles."""

    def constraints(
        operation: Device.Operation,
    ) -> list[tuple[list[float | None], list[tuple[float, float] | None]]]:
        return [] if isinstance(operation, Mock) else _operation_parameter_constraints(operation)

    monkeypatch.setattr(qdmi_backend, "_operation_parameter_constraints", constraints)


@pytest.fixture
def serialization_target(monkeypatch: pytest.MonkeyPatch) -> None:
    """Use actual native serialization with the tests' synthetic device sites."""

    def serialize(
        circuit: QuantumCircuit,
        *,
        target: Device,
        program_format: ProgramFormat,
        site_indices: tuple[int, ...] | None,
    ) -> CompiledProgram:
        snapshot = CompilerTarget(
            [CompilerTarget.Site(index, site.name()) for index, site in enumerate(target.sites())],
            connectivity=CompilerTarget.Connectivity.all_to_all(),
            native_operations=CompilerTarget.NativeOperations.unrestricted(),
        )
        return serialize_program(circuit, target=snapshot, program_format=program_format, site_indices=site_indices)

    monkeypatch.setattr(serializers, "serialize_program", serialize)
