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

"""IQM run-request option validation and submission tests."""

from __future__ import annotations

import json
import math
from typing import Any
from unittest.mock import Mock

import pytest
from mqt.core.plugins.qiskit import CircuitValidationError, QDMIBackend
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit.circuit import ClassicalRegister, Measure, Parameter, QuantumCircuit, QuantumRegister
from qiskit.circuit.library import RGate
from qiskit.transpiler import Target

from iqm.qdmi.qiskit import IQMBackend


@pytest.fixture
def backend(monkeypatch: pytest.MonkeyPatch) -> tuple[IQMBackend, Mock]:
    """Returns a real backend and its device submission mock."""
    device = Mock(spec=Device)
    device.name.return_value = "IQM option test"
    device.version.return_value = "test"
    sites = [Mock() for _ in range(4)]
    for index, (site, name) in enumerate(zip(sites, ["QB7", "QB2", "QB11", "QB5"], strict=True)):
        site.index.return_value = index
        site.name.return_value = name
    device.sites.return_value = sites
    operations = [Mock(), Mock()]
    for operation, name in zip(operations, ["measure", "prx"], strict=True):
        operation.name.return_value = name
        operation.is_zoned.return_value = False
    device.operations.return_value = operations
    device.supported_program_formats.return_value = [ProgramFormat.IQM_JSON]
    device.try_submit_job.return_value = Mock(spec=Job)
    target = Target(num_qubits=4)
    target.add_instruction(Measure(), {(index,): None for index in range(4)})
    target.add_instruction(RGate(Parameter("theta"), Parameter("phi")), {(index,): None for index in range(4)})
    monkeypatch.setattr(IQMBackend, "_build_target", lambda _self: target)
    monkeypatch.setattr(IQMBackend, "_serialize_circuit", lambda *_args: ("{}", ProgramFormat.IQM_JSON))
    return IQMBackend(device=device), device


@pytest.mark.parametrize(
    "value",
    [
        [],
        {"circuits": []},
        {"shots": 2},
        {"calibration_set_id": "other"},
        {"nested": object()},
        {"nested": float("nan")},
        {"heralding_mode": "zeros"},
    ],
)
def test_reject_invalid_run_request_options(backend: tuple[IQMBackend, Mock], value: object) -> None:
    """Reject malformed or reserved fields before creating a device job."""
    iqm_backend, device = backend
    with pytest.raises(CircuitValidationError):
        iqm_backend.run(QuantumCircuit(1), run_request_options=value)
    device.try_submit_job.assert_not_called()
    device.submit_job.assert_not_called()


def test_run_request_defaults_and_overrides(backend: tuple[IQMBackend, Mock]) -> None:
    """Forward one options object per native batch, preserving backend defaults."""
    iqm_backend, device = backend
    defaults = {"dd_mode": "enabled", "future_server_field": {"nested": [1, True]}}
    circuit = QuantumCircuit(1, 1)
    circuit.measure(0, 0)
    cases: list[tuple[object, dict[str, Any], object]] = [
        (None, {}, None),
        ({"heralding_mode": "none"}, {}, {"heralding_mode": "none"}),
        (defaults, {"memory": False}, defaults),
        (defaults, {"run_request_options": {"active_reset_cycles": 2}}, {"active_reset_cycles": 2}),
        (defaults, {"run_request_options": None}, None),
        (defaults, {"run_request_options": {}}, {}),
    ]
    for default, overrides, expected in cases:
        iqm_backend.set_options(run_request_options=default)
        iqm_backend.run([circuit, circuit], shots=10, **overrides)
        submission = device.try_submit_job
        submission.assert_called_once()
        assert submission.call_args.args == (["{}", "{}"], ProgramFormat.IQM_JSON, 10)
        params = submission.call_args.kwargs
        assert (json.loads(params["custom1"]) if params else None) == expected
        assert iqm_backend.options.run_request_options == default
        assert iqm_backend.options.memory is True
        submission.reset_mock()


@pytest.mark.parametrize("memory", [False, True])
@pytest.mark.usefixtures("serialization_target")
def test_classical_result_destinations(
    backend: tuple[IQMBackend, Mock], monkeypatch: pytest.MonkeyPatch, *, memory: bool
) -> None:
    """Normalized readout uses Core's counts and memory decoding unchanged."""
    iqm_backend, device = backend
    monkeypatch.setattr(IQMBackend, "_serialize_circuit", QDMIBackend._serialize_circuit)  # ruff:ignore[private-member-access]
    handle = Mock(spec=Job)
    handle.get_program_status.return_value = None
    handle.check.return_value = Job.Status.DONE

    def submit(program: str, _format: ProgramFormat, shots: int) -> Mock:
        instructions = json.loads(program)["instructions"]
        bits = "".join(
            "1" if instruction["locus"][0] in {"QB7", "QB11"} else "0"
            for instruction in instructions
            if instruction["name"] == "measure"
        )[::-1]
        handle.get_shots.return_value = [bits] * shots
        handle.get_counts.return_value = {bits: shots}
        return handle

    device.submit_job.side_effect = submit
    circuit = QuantumCircuit(QuantumRegister(4), ClassicalRegister(3, "zeta"), ClassicalRegister(1, "alpha"))
    circuit.r(math.pi, 0, [0, 2])
    circuit.measure([0, 1, 2, 3], [2, 0, 3, 1])

    result = iqm_backend.run(circuit, shots=3, memory=memory).result()
    program = json.loads(device.submit_job.call_args.args[0])
    assert {instruction["locus"][0] for instruction in program["instructions"] if instruction["name"] == "measure"} == {
        "QB2",
        "QB5",
        "QB7",
        "QB11",
    }
    # Only q[0] and q[2] are one: zeta[2] = alpha[0] = 1.
    assert result.get_counts() == {"1 100": 3}
    if memory:
        assert result.get_memory() == ["1 100"] * 3
