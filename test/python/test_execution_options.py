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

"""IQM execution option validation and submission tests."""

from __future__ import annotations

import json
from unittest.mock import Mock

import pytest
from mqt.core.plugins.qiskit import CircuitValidationError, QDMIBackend
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit.circuit import Measure, QuantumCircuit
from qiskit.transpiler import Target

from iqm.qdmi.options import execution_parameters
from iqm.qdmi.qiskit import IQMBackend


def test_encode_all_execution_options() -> None:
    """Encode each supported option with its native type and field name."""
    parameters = execution_parameters({
        "heralding_mode": "zeros",
        "move_gate_validation": "allow_prx",
        "move_gate_frame_tracking": "no_detuning_correction",
        "dd_mode": "enabled",
        "qubit_mapping": {"q0": "QB1", "q1": "QB2"},
        "max_circuit_duration_over_t2": 0.5,
        "active_reset_cycles": 2,
        "dd_strategy": {"merge_contiguous_waits": True},
    })
    assert parameters["custom1"] == "zeros"
    assert parameters["custom2"] == "allow_prx"
    assert parameters["custom3"] == "no_detuning_correction"
    assert parameters["custom5"] == "q0:QB1,q1:QB2"
    assert json.loads(str(parameters["custom4"])) == {
        "iqm_execution_options": 1,
        "dd_mode": "enabled",
        "max_circuit_duration_over_t2": 0.5,
        "active_reset_cycles": 2,
        "dd_strategy": {"merge_contiguous_waits": True},
    }


@pytest.mark.parametrize(
    ("name", "value"),
    [
        ("unknown", "value"),
        ("heralding_mode", "typo"),
        ("dd_mode", True),
        ("move_gate_validation", []),
        ("move_gate_frame_tracking", "disabled"),
        ("qubit_mapping", {}),
        ("qubit_mapping", {"q:0": "QB1"}),
        ("qubit_mapping", {"q0": "QB1,other"}),
        ("qubit_mapping", {"q0": ""}),
        ("qubit_mapping", {"q0": 1}),
        ("qubit_mapping", {"q0": "QB1\0"}),
        ("max_circuit_duration_over_t2", 0),
        ("max_circuit_duration_over_t2", 10**400),
        ("max_circuit_duration_over_t2", float("nan")),
        ("max_circuit_duration_over_t2", float("inf")),
        ("max_circuit_duration_over_t2", True),
        ("active_reset_cycles", -1),
        ("active_reset_cycles", 1.5),
        ("active_reset_cycles", True),
        ("active_reset_cycles", 2**128),
        ("dd_strategy", "{}"),
        ("dd_strategy", []),
        ("dd_strategy", {1: "value"}),
        ("dd_strategy", {"nested": [{1: "value"}]}),
        ("dd_strategy", {"value": object()}),
        ("dd_strategy", {"value": float("nan")}),
    ],
)
def test_reject_invalid_execution_options(name: str, value: object) -> None:
    """Reject misspellings, coercions, and non-JSON values before submission."""
    with pytest.raises(CircuitValidationError):
        execution_parameters({name: value})


def test_unset_execution_options_preserve_native_defaults() -> None:
    """Omitted settings leave all native options unchanged."""
    assert all(value is None for value in execution_parameters({}).values())
    assert execution_parameters({"dd_mode": "enabled"})["custom4"] == "enabled"
    assert execution_parameters({"active_reset_cycles": 0})["custom4"] is not None


def test_older_mqt_rejects_new_options(monkeypatch: pytest.MonkeyPatch) -> None:
    """An older backend must never accept new options without forwarding them."""
    monkeypatch.delattr(QDMIBackend, "_job_parameters", raising=False)
    options = IQMBackend._default_options()  # ruff:ignore[private-member-access]
    assert set(options) == {"shots", "memory"}
    backend = IQMBackend.__new__(IQMBackend)
    backend._options = options  # ruff:ignore[private-member-access]
    with pytest.raises(CircuitValidationError, match="heralding_mode"):
        backend.run(QuantumCircuit(1), heralding_mode="zeros")
    with pytest.raises(AttributeError):
        backend.set_options(heralding_mode="zeros")


@pytest.mark.skipif(not hasattr(QDMIBackend, "_job_parameters"), reason="requires MQT job-option hook")
def test_iqm_backend_forwards_options_for_every_circuit(monkeypatch: pytest.MonkeyPatch) -> None:
    """Exercise IQM option encoding through the generic MQT submission path."""
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
    backend.set_options(heralding_mode="zeros", active_reset_cycles=2)
    circuit = QuantumCircuit(1, 1)
    circuit.measure(0, 0)
    backend.run([circuit, circuit], shots=10, heralding_mode="none", dd_mode="enabled")
    assert device.submit_job.call_count == 2
    for call in device.submit_job.call_args_list:
        assert call.kwargs["custom1"] == "none"
        assert json.loads(call.kwargs["custom4"]) == {
            "iqm_execution_options": 1,
            "dd_mode": "enabled",
            "active_reset_cycles": 2,
        }
        assert call.kwargs["num_shots"] == 10
    assert backend.options.heralding_mode == "zeros"
    device.submit_job.reset_mock()
    with pytest.raises(CircuitValidationError, match="heralding_mode"):
        backend.run(circuit, heralding_mode="bad")
    with pytest.raises(CircuitValidationError, match="Unsupported execution options"):
        backend.run(circuit, heralding="zeros")
    device.submit_job.assert_not_called()
