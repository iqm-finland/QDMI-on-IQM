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
from unittest.mock import MagicMock

import pytest
from qiskit import QuantumCircuit, transpile
from qiskit.primitives import StatevectorSampler
from qiskit.providers.fake_provider import GenericBackendV2
from qiskit.transpiler import CouplingMap

if TYPE_CHECKING:
    from types import ModuleType


@pytest.fixture
def showcase(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    """Load the helper from the example script directory.

    Returns:
        The shared showcase helper.
    """
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2] / "examples"))
    return importlib.import_module("showcase_backend")


@pytest.mark.parametrize("iteration_mode", [False, True])
def test_showcase_uses_selected_backend(
    showcase: ModuleType, monkeypatch: pytest.MonkeyPatch, *, iteration_mode: bool
) -> None:
    """Only reserved runs enter the IQM reservation context."""
    factory = MagicMock()
    backend = factory.return_value
    reserved = object()
    backend.reservation.return_value = nullcontext(reserved)
    monkeypatch.setattr(showcase, "IQMBackend", factory)
    with showcase.showcase_backend("iqm", num_qubits=4, iteration_mode=iteration_mode) as actual:
        assert actual is (reserved if iteration_mode else backend)
    if iteration_mode:
        backend.reservation.assert_called_once_with(max_duration_seconds=900, idle_timeout_seconds=120)
    else:
        backend.reservation.assert_not_called()


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
    logical.cx(0, 2)
    target = GenericBackendV2(num_qubits=6, coupling_map=CouplingMap.from_line(6), seed=1)
    routed = transpile(logical, backend=target, initial_layout=[5, 3, 1, 0])
    assert routed.layout is not None
    assert routed.layout.final_layout is not None
    showcase.measure_logical_qubits(routed, logical.num_qubits)

    result = StatevectorSampler(default_shots=16, seed=1).run([routed]).result()
    assert result[0].data["meas"].get_counts() == {"0111": 16}
