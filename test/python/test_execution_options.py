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

from typing import TYPE_CHECKING, Any

import pytest
from mqt.core.plugins.qiskit import CircuitValidationError
from qiskit.circuit import QuantumCircuit

if TYPE_CHECKING:
    from iqm.qdmi.qiskit import IQMBackend


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
def test_reject_invalid_run_request_options(
    offline_backend: IQMBackend, offline_submissions: list[dict], value: object
) -> None:
    """Reject malformed or reserved fields before creating a device job."""
    with pytest.raises(CircuitValidationError):
        offline_backend.run(QuantumCircuit(1), run_request_options=value)
    assert not offline_submissions


def test_run_request_defaults_and_overrides(offline_backend: IQMBackend, offline_submissions: list[dict]) -> None:
    """Forward one options object per native batch, preserving backend defaults."""
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
        offline_backend.set_options(run_request_options=default)
        offline_backend.run([circuit, circuit], shots=10, **overrides)
        assert len(offline_submissions) == 1
        submission = offline_submissions.pop()
        assert len(submission.pop("circuits")) == 2
        assert submission.pop("shots") == 10
        assert submission.pop("calibration_set_id") == offline_backend.calibration_set_id
        assert submission == (expected or {})
        assert offline_backend.options.run_request_options == default
        assert offline_backend.options.memory is True
