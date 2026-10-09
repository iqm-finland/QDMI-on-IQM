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
from unittest.mock import Mock

import pytest
from mqt.core.plugins.qiskit.backend import QDMIBackend
from mqt.core.plugins.qiskit.exceptions import CircuitValidationError, UnsupportedDeviceError
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit import QuantumCircuit, transpile

from iqm.qdmi.qiskit import IQMBackend
from iqm.qdmi.serializers import qiskit_to_iqm_json


def _device(*, missing_gate: str = "prx", missing_index: int | None = 1, resonator: bool = False) -> Mock:
    device = Mock(spec=Device)
    device.name.return_value = "IQM calibration test"
    device.version.return_value = "test"
    device.qubits_num.return_value = 3
    sites = [Mock() for _ in range(4 if resonator else 3)]
    for index, site in enumerate(sites):
        site.index.return_value = index
        site.name.return_value = f"QB{index + 1}" if index < 3 else "CR1"
    device.sites.return_value = sites
    operations = []
    for name in ["prx", "measure", "cz", *(["move"] if resonator else [])]:
        op = Mock()
        op.name.return_value = name
        op.is_zoned.return_value = False
        op.qubits_num.return_value = 1 if name in {"prx", "measure"} else 2
        op.duration.return_value = None
        op.fidelity.return_value = 0.97
        op.sites.return_value = [site for i, site in enumerate(sites[:3]) if name != missing_gate or i != missing_index]
        op.site_pairs.return_value = (
            [(sites[0], sites[3]), (sites[1], sites[3]), (sites[2], sites[3])]
            if name == "move"
            else [(sites[0], sites[1]), (sites[0], sites[2]), (sites[1], sites[2])]
        )
        operations.append(op)
    device.operations.return_value = operations
    device.supported_program_formats.return_value = [ProgramFormat.IQM_JSON]
    device.submit_job.return_value = Mock(spec=Job)
    device.try_submit_job.return_value = Mock(spec=Job)
    return device


@pytest.mark.parametrize("missing_gate", ["prx", "measure"])
@pytest.mark.parametrize(("missing_index", "usable"), [(0, [1, 2]), (1, [0, 2]), (2, [0, 1])])
def test_transpile_uses_calibrated_qubits(missing_gate: str, missing_index: int, usable: list[int]) -> None:
    """A missing calibration must not prevent using the remaining connected pair."""
    device = _device(missing_gate=missing_gate, missing_index=missing_index)
    backend = IQMBackend(device=device)
    assert backend.num_qubits == 2
    assert backend.physical_qubits == tuple(usable)
    circuit = QuantumCircuit(2, 2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure([0, 1], [0, 1])
    compiled = transpile(circuit, backend, optimization_level=2, seed_transpiler=7)
    backend.run([compiled, compiled], shots=8)
    programs = device.try_submit_job.call_args.args[0]
    assert len(programs) == 2
    for program in programs:
        instructions = json.loads(program)["instructions"]
        assert {site for instruction in instructions for site in instruction["locus"]} == {f"QB{i + 1}" for i in usable}
    assert backend.target["r"][0,].error == pytest.approx(0.03)


def test_submission_restores_physical_sites_and_classical_destinations() -> None:
    """Compacting away QB2 must preserve QB3 and the caller's classical mapping."""
    device = _device()
    backend = IQMBackend(device=device)
    circuit = QuantumCircuit(2, 2, name="mapped")
    circuit.r(0.3, 0.4, 1)
    circuit.cz(0, 1)
    circuit.measure([0, 1], [1, 0])
    circuit.metadata = {"label": "keep"}
    original = circuit.copy()
    backend.run(circuit, shots=8)
    program = json.loads(device.submit_job.call_args.args[0])
    assert json.loads(qiskit_to_iqm_json(circuit, backend)) == program
    assert [(inst["name"], inst["locus"]) for inst in program["instructions"]] == [
        ("prx", ["QB3"]),
        ("cz", ["QB1", "QB3"]),
        ("measure", ["QB1"]),
        ("measure", ["QB3"]),
    ]
    assert [inst["args"]["key"] for inst in program["instructions"] if inst["name"] == "measure"] == [
        "c_2_0_1",
        "c_2_0_0",
    ]
    assert program["name"] == "mapped"
    assert program["metadata"] == {"label": "keep"}
    assert circuit == original
    assert circuit.num_qubits == 2


def test_generic_backend_keeps_raw_device_sites() -> None:
    """The generic QDMI backend still uses its unfiltered target's site order."""
    backend = QDMIBackend(device=_device())
    circuit = QuantumCircuit(3)
    circuit.r(0.3, 0.4, 1)
    program = json.loads(qiskit_to_iqm_json(circuit, backend))
    assert program["instructions"][0]["locus"] == ["QB2"]


def test_target_preserves_resonator_and_move_sites() -> None:
    """Resonators do not need PRX or measurement calibrations."""
    device = _device(resonator=True)
    backend = IQMBackend(device=device)
    assert backend.num_qubits == 3
    assert set(backend.target["move"]) == {(0, 2), (1, 2)}
    circuit = QuantumCircuit(3)
    circuit.append(backend.target.operation_from_name("move"), [1, 2])
    backend.run(circuit)
    program = json.loads(device.submit_job.call_args.args[0])
    assert program["instructions"][0]["locus"] == ["QB3", "CR1"]


def test_reject_circuit_wider_than_calibrated_target() -> None:
    """Raw physical indices must not bypass the compact target's index mapping."""
    device = _device()
    backend = IQMBackend(device=device)
    with pytest.raises(CircuitValidationError):
        backend.run(QuantumCircuit(3))
    device.submit_job.assert_not_called()
    device.try_submit_job.assert_not_called()


def test_reject_target_without_usable_qubits() -> None:
    """Missing required operations must produce a clear construction error."""
    device = _device()
    device.operations.return_value = [op for op in device.operations() if op.name() != "prx"]
    with pytest.raises(UnsupportedDeviceError, match="calibrated"):
        IQMBackend(device=device)


def test_fully_calibrated_target_keeps_physical_indices() -> None:
    """Fully calibrated backends retain their existing qubit numbering."""
    device = _device(missing_index=None)
    backend = IQMBackend(device=device)
    assert backend.num_qubits == 3
    circuit = QuantumCircuit(3)
    circuit.r(0.3, 0.4, 2)
    backend.run(circuit)
    program = json.loads(device.submit_job.call_args.args[0])
    assert program["instructions"][0]["locus"] == ["QB3"]
