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
from typing import Any
from unittest.mock import Mock

import pytest
from mqt.core.plugins.qiskit import CircuitValidationError
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit.circuit import Measure, QuantumCircuit
from qiskit.transpiler import Target

from iqm.qdmi.qiskit import IQMBackend


@pytest.fixture
def backend(monkeypatch: pytest.MonkeyPatch) -> tuple[IQMBackend, Mock]:
    """Returns a real backend and its device submission mock."""
    device = Mock(spec=Device)
    device.name.return_value = "IQM option test"
    device.version.return_value = "test"
    operation = Mock()
    operation.name.return_value = "measure"
    operation.is_zoned.return_value = False
    device.operations.return_value = [operation]
    device.supported_program_formats.return_value = [ProgramFormat.IQM_JSON]
    device.try_submit_job.return_value = Mock(spec=Job)
    target = Target(num_qubits=1)
    target.add_instruction(Measure(), {(0,): None})
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
        (defaults, {}, defaults),
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
        submission.reset_mock()
