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

"""Tests for the serialization of Qiskit circuits into IQM JSON."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

from mqt.core.mlir import CompiledProgram, TargetEnvironment, serialize_program
from qiskit.circuit import QuantumCircuit

if TYPE_CHECKING:
    from iqm.qdmi.qiskit import IQMBackend


def test_native_device_serialization(offline_backend: IQMBackend) -> None:
    """Use the actual IQM QDMI handle and MQT Core's native device snapshot."""
    qc = QuantumCircuit(2, 3)
    qc.r(1.0, 0.0, 0)
    qc.cz(0, 1)
    qc.measure(1, 2)
    program = serialize_program(qc, target=TargetEnvironment.from_device(offline_backend.device))
    assert isinstance(program, CompiledProgram)
    instructions = json.loads(program.payload)["instructions"]
    assert instructions[0] == {"name": "prx", "locus": ["QB1"], "args": {"angle": 1.0, "phase": 0.0}}
    assert type(instructions[0]["args"]["angle"]) is float
    assert type(instructions[0]["args"]["phase"]) is float
    assert instructions[-1]["locus"] == ["QB2"]
    assert program.result_layout is not None
    assert program.result_layout.decode_bits("1") == "100"
