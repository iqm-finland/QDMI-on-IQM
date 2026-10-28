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
from mqt.core.mlir import CompiledProgram, compile_program
from mqt.core.plugins.qiskit.exceptions import TranslationError, UnsupportedDeviceError
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit import QuantumCircuit, transpile

from iqm.qdmi.qiskit import IQMBackend
from iqm.qdmi.serializers import qiskit_to_iqm_json

pytestmark = pytest.mark.usefixtures("serialization_target")


def _device(*, missing_gate: str | None = "prx", resonator: bool = False) -> Mock:
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
        op.query_custom_property.return_value = None
        op.parameters_num.return_value = 2 if name == "prx" else 0
        op.name.return_value = name
        op.is_zoned.return_value = False
        op.qubits_num.return_value = 1 if name in {"prx", "measure"} else 2
        op.duration.return_value = None
        op.fidelity.return_value = 0.97
        op.sites.return_value = [site for i, site in enumerate(sites[:3]) if name != missing_gate or i != 1]
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


@pytest.mark.parametrize("missing_gate", [None, "prx", "measure"])
@pytest.mark.parametrize("compile_with_mqt", [False, True])
def test_transpile_and_submit_calibrated_qubits(missing_gate: str | None, *, compile_with_mqt: bool) -> None:
    """Transpilation and both serialization paths address calibrated physical sites."""
    device = _device(missing_gate=missing_gate)
    backend = IQMBackend(device=device)
    usable = (0, 2) if missing_gate else (0, 1, 2)
    assert backend.num_qubits == 3
    circuit = QuantumCircuit(2, 2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure([0, 1], [1, 0])
    compiled = (
        compile_program(circuit, target=backend)
        if compile_with_mqt
        else transpile(circuit, backend, optimization_level=2, seed_transpiler=7)
    )
    layout = compiled.layout
    backend.run([compiled, compiled], shots=8)
    assert compiled.layout is layout
    program = qiskit_to_iqm_json(compiled, backend)
    assert isinstance(program, CompiledProgram)
    assert device.try_submit_job.call_args.args[0] == [program.payload, program.payload]
    instructions = json.loads(program.payload)["instructions"]
    used_sites = {site for instruction in instructions for site in instruction["locus"]}
    assert len(used_sites) == 2
    assert used_sites <= {f"QB{i + 1}" for i in usable}
    assert compiled.layout is not None
    measured = [inst for inst in instructions if inst["name"] == "measure"]
    for logical, index in enumerate(compiled.layout.final_index_layout()):
        raw_slot = program.result_layout.bits[1 - logical] if program.result_layout else 1 - logical
        assert type(raw_slot) is int
        assert measured[raw_slot]["locus"] == [f"QB{index + 1}"]
    assert backend.target["r"][0,].error == pytest.approx(0.03)


def test_target_preserves_resonator_and_move_sites() -> None:
    """Resonators do not need PRX or measurement calibrations."""
    device = _device(resonator=True)
    backend = IQMBackend(device=device)
    assert backend.num_qubits == 4
    assert set(backend.target["move"]) == {(0, 3), (2, 3)}
    circuit = QuantumCircuit(4)
    circuit.append(backend.target.operation_from_name("move"), [2, 3])
    backend.run(circuit)
    program = json.loads(device.submit_job.call_args.args[0])
    assert program["instructions"][0]["locus"] == ["QB3", "CR1"]


def test_reject_operation_outside_target() -> None:
    """An unaddressable target index fails before submission."""
    device = _device()
    backend = IQMBackend(device=device)
    circuit = QuantumCircuit(3)
    circuit.r(0.3, 0.4, 1)
    with pytest.raises(TranslationError):
        backend.run([QuantumCircuit(2), circuit])
    device.submit_job.assert_not_called()
    device.try_submit_job.assert_not_called()


def test_reject_target_without_usable_qubits() -> None:
    """Missing required operations must produce a clear construction error."""
    device = _device()
    device.operations.return_value = [op for op in device.operations() if op.name() != "prx"]
    with pytest.raises(UnsupportedDeviceError, match="calibrated"):
        IQMBackend(device=device)
