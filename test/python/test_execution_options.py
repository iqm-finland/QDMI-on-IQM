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
from unittest.mock import Mock

import pytest
from mqt.core.plugins.qiskit import CircuitValidationError, QDMIBackend
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit.circuit import Measure, QuantumCircuit
from qiskit.transpiler import Target

from iqm.qdmi.qiskit import IQMBackend, execution_parameters


def test_encode_partial_run_request() -> None:
    """Forward arbitrary IQM fields as one JSON object without a local schema."""
    fields = {
        "heralding_mode": "zeros",
        "dd_mode": "enabled",
        "max_circuit_duration_over_t2": 0.0,
        "qubit_mapping": [{"logical_name": "alice", "physical_name": "QB1"}],
        "future_server_field": {"nested": [1, True]},
    }
    assert execution_parameters({"run_request_options": fields}) == {"custom1": json.dumps(fields)}


@pytest.mark.parametrize(
    "value",
    [
        [],
        "{}",
        {"circuits": []},
        {"shots": 2},
        {"calibration_set_id": "other"},
        {"nested": object()},
        {"nested": float("nan")},
        {"nested": float("inf")},
    ],
)
def test_reject_invalid_run_request_options(value: object) -> None:
    """Reject non-JSON objects and QDMI-owned fields before submission."""
    with pytest.raises(CircuitValidationError):
        execution_parameters({"run_request_options": value})


def test_unset_run_request_uses_server_defaults() -> None:
    """An unset mapping does not occupy a QDMI custom parameter."""
    assert execution_parameters({}) == {"custom1": None}
    assert execution_parameters({"run_request_options": None}) == {"custom1": None}
    assert execution_parameters({"run_request_options": {}}) == {"custom1": "{}"}
    with pytest.raises(CircuitValidationError, match="Unsupported execution options"):
        execution_parameters({"heralding_mode": "zeros"})


def test_reject_circular_run_request_options() -> None:
    """The JSON encoder rejects a circular mapping."""
    fields: dict[str, object] = {}
    fields["nested"] = fields
    with pytest.raises(CircuitValidationError, match="JSON-compatible"):
        execution_parameters({"run_request_options": fields})


def test_older_mqt_rejects_new_options(monkeypatch: pytest.MonkeyPatch) -> None:
    """An older backend must never accept new options without forwarding them."""
    monkeypatch.delattr(QDMIBackend, "_job_parameters", raising=False)
    options = IQMBackend._default_options()  # ruff:ignore[private-member-access]
    assert "run_request_options" not in options
    backend = IQMBackend.__new__(IQMBackend)
    backend._options = options  # ruff:ignore[private-member-access]
    with pytest.raises(CircuitValidationError, match="run_request_options"):
        backend.run(QuantumCircuit(1), run_request_options={"dd_mode": "enabled"})
    with pytest.raises(AttributeError):
        backend.set_options(run_request_options={"dd_mode": "enabled"})


@pytest.mark.skipif(not hasattr(QDMIBackend, "_job_parameters"), reason="requires MQT job-option hook")
def test_iqm_backend_forwards_options_for_every_circuit(monkeypatch: pytest.MonkeyPatch) -> None:
    """Exercise the JSON option through the generic MQT submission path."""
    device = Mock(spec=Device)
    device.name.return_value = "IQM option test"
    device.version.return_value = "test"
    operation = Mock()
    operation.name.return_value = "measure"
    operation.is_zoned.return_value = False
    device.operations.return_value = [operation]
    device.supported_program_formats.return_value = [ProgramFormat.IQM_JSON]
    job = Mock(spec=Job)
    job.id = "options-job"
    device.submit_job.return_value = job
    target = Target(num_qubits=1)
    target.add_instruction(Measure(), {(0,): None})
    monkeypatch.setattr("iqm.qdmi.qiskit.register_device_if_absent", lambda _definition: False)
    monkeypatch.setattr("iqm.qdmi.qiskit.open_device", lambda *_args, **_kwargs: device)
    monkeypatch.setattr(IQMBackend, "_build_target", lambda _self: target)
    monkeypatch.setattr(IQMBackend, "_serialize_circuit", lambda *_args: ("{}", ProgramFormat.IQM_JSON))
    backend = IQMBackend()
    backend.set_options(run_request_options={"heralding_mode": "zeros"})
    circuit = QuantumCircuit(1, 1)
    circuit.measure(0, 0)
    backend.run([circuit, circuit], shots=10, run_request_options={"dd_mode": "enabled"})
    assert device.submit_job.call_count == 2
    for call in device.submit_job.call_args_list:
        assert json.loads(call.kwargs["custom1"]) == {"dd_mode": "enabled"}
        assert call.kwargs["num_shots"] == 10
    assert backend.options.run_request_options == {"heralding_mode": "zeros"}
    device.submit_job.reset_mock()
    with pytest.raises(CircuitValidationError, match="run_request_options"):
        backend.run(circuit, run_request_options={"shots": 2})
    with pytest.raises(CircuitValidationError, match="Unsupported execution options"):
        backend.run(circuit, heralding_mode="zeros")
    device.submit_job.assert_not_called()
