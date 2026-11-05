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

"""Calibrated Qiskit targets and physical IQM submission indices."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

import pytest
from mqt.core.mlir import CompiledProgram
from qiskit import QuantumCircuit

if TYPE_CHECKING:
    from iqm.qdmi.qiskit import IQMBackend


@pytest.mark.parametrize(
    ("offline_backend", "calibrated"),
    [(None, True), ("missing_prx", False), ("missing_measure", False)],
    indirect=["offline_backend"],
)
def test_compile_calibrated_qubits(offline_backend: IQMBackend, *, calibrated: bool) -> None:
    """Placement uses only sites with both PRX and measurement calibration."""
    assert ((0,) in offline_backend.target["measure"]) is calibrated
    usable = set(offline_backend.target["r"]) & set(offline_backend.target["measure"])
    assert usable
    circuit = QuantumCircuit(1, 1)
    circuit.h(0)
    circuit.measure(0, 0)
    compiled = offline_backend.compile(circuit)
    assert isinstance(compiled, CompiledProgram)
    instructions = json.loads(compiled.payload)["instructions"]
    expected = {f"QB{site[0] + 1}" for site in usable}
    assert {site for instruction in instructions for site in instruction["locus"]} <= expected


@pytest.mark.parametrize("offline_backend", ["resonator"], indirect=True)
def test_compile_and_submit_sirius(offline_backend: IQMBackend, offline_submissions: list[dict]) -> None:
    """Compile a Bell circuit through a resonator and submit only to localhost."""
    assert "move" not in offline_backend.target.operation_names
    assert "cz" not in offline_backend.target.operation_names
    circuit = QuantumCircuit(2, 2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure([0, 1], [0, 1])
    job = offline_backend.run(offline_backend.compile(circuit), shots=4)
    assert job.job_id() == "offline-job"
    assert job.result().get_counts() == {"00": 2, "11": 2}
    assert len(offline_submissions) == 1
    instructions = offline_submissions[0]["circuits"][0]["instructions"]
    names = [instruction["name"] for instruction in instructions]
    assert names.count("move") == 2
    first_measure = names.index("measure")
    assert "move" not in names[first_measure:]
    assert offline_submissions[0]["shots"] == 4


@pytest.mark.parametrize("offline_backend", ["extra_operations"], indirect=True)
def test_target_accepts_iqm_extra_operations(offline_backend: IQMBackend) -> None:
    """Extra native operations do not prevent constructing the shared Core target."""
    assert {"prx_12", "measure_012", "r", "measure"} <= offline_backend.target.operation_names
