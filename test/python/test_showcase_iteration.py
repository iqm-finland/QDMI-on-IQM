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

"""Regression tests for iteration-enabled examples."""

from __future__ import annotations

import argparse
import importlib
from contextlib import nullcontext
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import MagicMock, Mock

import pytest
from qiskit import QuantumCircuit, transpile
from qiskit.primitives import StatevectorSampler
from qiskit.providers.fake_provider import GenericBackendV2

from iqm.qdmi.iteration import IterationError, IterationResult

if TYPE_CHECKING:
    from types import ModuleType

ITERATION_ID = "12345678-1234-1234-1234-123456789abc"


@pytest.fixture
def showcase(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    """Load the helper from the example script directory.

    Returns:
        The shared showcase helper.
    """
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2] / "examples"))
    return importlib.import_module("showcase_backend")


@pytest.fixture
def reservation(showcase: ModuleType, monkeypatch: pytest.MonkeyPatch) -> tuple[MagicMock, Mock]:
    """Stub transport while preserving the examples' lifecycle and control flow.

    Returns:
        The management client and backend factory.
    """
    client = MagicMock()
    client.__enter__.return_value = client
    client.create.return_value = IterationResult(ITERATION_ID, "waiting", 200)
    client.end.return_value = IterationResult(ITERATION_ID, "active", 202)
    client.get.side_effect = [
        IterationResult(ITERATION_ID, "active", 200),
        IterationResult(ITERATION_ID, "completed", 200),
    ]
    monkeypatch.setattr(showcase, "IterationClient", Mock(return_value=client))
    backend = Mock()
    monkeypatch.setattr(showcase, "IQMBackend", backend)
    monkeypatch.setattr(showcase, "sleep", Mock())
    monkeypatch.setenv("IQM_SERVER_URL", "https://unit.example")
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", "emerald:mock")
    monkeypatch.delenv("IQM_QC_ID", raising=False)
    return client, backend


@pytest.mark.parametrize("failure", [None, RuntimeError("workload failed"), KeyboardInterrupt()])
def test_showcase_ends_reservation(
    showcase: ModuleType, reservation: tuple[MagicMock, Mock], failure: BaseException | None
) -> None:
    """Use one attached backend and confirm termination after every workload exit."""
    client, backend = reservation
    expectation = pytest.raises(type(failure)) if failure is not None else nullcontext()
    with expectation, showcase.showcase_backend("iqm", num_qubits=4, iteration_mode=True) as device:
        assert device is backend.return_value
        client.end.assert_not_called()
        if failure is not None:
            raise failure

    backend.assert_called_once_with(
        base_url="https://unit.example", qc_id=None, qc_alias="emerald:mock", iteration_id=ITERATION_ID
    )
    client.create.assert_called_once_with("emerald:mock", 900, idle_timeout_seconds=120)
    client.end.assert_called_once_with(ITERATION_ID)
    assert [call.args for call in client.get.call_args_list] == [(ITERATION_ID,), (ITERATION_ID,)]


def test_expired_activation_still_ends_reservation(
    showcase: ModuleType, reservation: tuple[MagicMock, Mock], monkeypatch: pytest.MonkeyPatch
) -> None:
    """An activation deadline never opens a backend and still cancels the reservation."""
    client, backend = reservation
    client.end.return_value = IterationResult(ITERATION_ID, "cancelled", 200)
    monkeypatch.setattr(showcase, "monotonic", Mock(side_effect=[0, 301, 301, 301]))

    with pytest.raises(TimeoutError), showcase.showcase_backend("iqm", num_qubits=4, iteration_mode=True):
        pytest.fail("No backend should open after the activation deadline.")

    backend.assert_not_called()
    client.get.assert_not_called()
    client.end.assert_called_once_with(ITERATION_ID)


@pytest.mark.parametrize("interrupted", [False, True])
def test_showcase_ends_partially_created_reservation(
    showcase: ModuleType, reservation: tuple[MagicMock, Mock], *, interrupted: bool
) -> None:
    """Attempt cleanup even when a failed creation response confirms the ID."""
    client, backend = reservation
    partial = IterationResult(ITERATION_ID, None, 200)
    error = KeyboardInterrupt() if interrupted else IterationError("create", -1, partial, {})
    if interrupted:
        error.__dict__["result"] = partial
    client.create.side_effect = error
    client.end.return_value = IterationResult(ITERATION_ID, "cancelled", 200)
    with pytest.raises(type(error)) as raised, showcase.showcase_backend("iqm", num_qubits=4, iteration_mode=True):
        pytest.fail("A partial reservation cannot be used.")
    assert raised.value is error
    client.end.assert_called_once_with(ITERATION_ID)
    backend.assert_not_called()


def test_reports_id_when_cleanup_fails(
    showcase: ModuleType, reservation: tuple[MagicMock, Mock], caplog: pytest.LogCaptureFixture
) -> None:
    """Keep the workload exception and report the reservation ID for recovery."""
    client, _ = reservation
    client.end.side_effect = OSError("end failed")
    msg = "workload failed"
    workload = Mock(side_effect=ValueError(msg))
    with pytest.raises(ValueError, match=msg), showcase.showcase_backend("iqm", num_qubits=4, iteration_mode=True):
        workload()

    assert ITERATION_ID in caplog.text


def test_rejects_iteration_mode_on_simulator(showcase: ModuleType, capsys: pytest.CaptureFixture[str]) -> None:
    """Reject an IQM reservation request through the shared CLI parser."""
    parser = argparse.ArgumentParser()
    showcase.add_backend_arguments(parser)
    args = parser.parse_args(["--backend", "sim", "--iteration-mode"])
    with pytest.raises(SystemExit) as raised:
        showcase.validate_backend_arguments(parser, args)
    assert raised.value.code == 2
    assert "--iteration-mode requires --backend iqm" in capsys.readouterr().err


def test_qsci_reads_every_logical_qubit_in_order(showcase: ModuleType) -> None:
    """Read logical sites in order, including idle sites and excluding hardware ancillas."""
    logical = QuantumCircuit(4)
    logical.x([0, 1])
    target = GenericBackendV2(num_qubits=6, seed=1)
    routed = transpile(logical, backend=target, initial_layout=[5, 3, 1, 0])
    showcase.measure_logical_qubits(routed, logical.num_qubits)

    result = StatevectorSampler(default_shots=16, seed=1).run([routed]).result()
    assert result[0].data["meas"].get_counts() == {"0011": 16}
