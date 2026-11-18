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

"""Configuration and execution through the shared PennyLane adapter."""

from __future__ import annotations

from typing import TYPE_CHECKING
from unittest.mock import patch

import pennylane as qml

from iqm.qdmi.pennylane import IQMDevice

if TYPE_CHECKING:
    import pytest
    from pennylane.measurements import SampleMP


def test_pennylane_session_defaults(monkeypatch: pytest.MonkeyPatch) -> None:
    """Both adapters use canonical endpoint defaults without changing named devices."""
    monkeypatch.setenv("IQM_SERVER_URL", "http://localhost:1234")
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", "emerald:mock")
    monkeypatch.setenv("IQM_TOKEN", "test-token")
    with patch("mqt.core.plugins.pennylane.QDMIDevice.__init__", return_value=None) as init:
        IQMDevice(wires=2)
        parameters = init.call_args.kwargs["session_parameters"]
        assert parameters["base_url"] == "http://localhost:1234"
        assert parameters["custom2"] == "emerald:mock"
        assert parameters["token"] == "test-token"  # ruff:ignore[hardcoded-password-string] Fake session token.
        IQMDevice(device_id="iqm.sirius.mock", wires=2)
        assert init.call_args.kwargs["session_parameters"]["base_url"] is None
        assert init.call_args.kwargs["session_parameters"]["custom2"] is None


def test_pennylane_entrypoint(monkeypatch: pytest.MonkeyPatch) -> None:
    """The installed entry point delegates compilation and execution to MQT Core."""
    monkeypatch.delenv("IQM_TOKEN", raising=False)
    monkeypatch.delenv("IQM_TOKENS_FILE", raising=False)
    device = qml.device("iqm.qdmi", device_id="mqt.ddsim.default", wires=2)

    @qml.qnode(device)
    def circuit() -> SampleMP:
        qml.PauliX(1)
        return qml.sample(wires=[1, 0])

    samples = qml.set_shots(circuit, shots=4)()
    assert samples.tolist() == [[1, 0]] * 4
