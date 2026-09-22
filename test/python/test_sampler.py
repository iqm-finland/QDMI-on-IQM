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

"""Tests for the QDMI-on-IQM sampler CLI script."""

from __future__ import annotations

import base64
import pickle  # ruff:ignore[suspicious-pickle-import]
from typing import TYPE_CHECKING

import pytest
from qiskit import ClassicalRegister, QuantumCircuit, qpy

from iqm.qdmi import offloader

if TYPE_CHECKING:
    from pathlib import Path

    from pytest_console_scripts import ScriptRunner


@pytest.mark.parametrize("register_sizes", [(3,), (2, 1)])
def test_sampler_cli_simulator(tmp_path: Path, script_runner: ScriptRunner, register_sizes: tuple[int, ...]) -> None:
    """The CLI and local sampler preserve all registers in Qiskit's bit order."""
    circuit = QuantumCircuit(3)
    circuit.add_register(*(ClassicalRegister(size, f"readout_{i}") for i, size in enumerate(register_sizes)))
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.x(2)
    circuit.measure(range(3), range(3))

    circuit_path = tmp_path / "bell.qpy"
    with circuit_path.open("wb") as file_obj:
        qpy.dump(circuit, file_obj)

    result = script_runner.run(["iqm-sampler", str(circuit_path), "--shots", "256", "--simulator"])
    assert result.success

    primitive_result = pickle.loads(base64.b64decode(result.stdout))  # ruff:ignore[suspicious-pickle-usage]
    counts = offloader.extract_counts(primitive_result)
    assert sum(counts.values()) == 256
    assert set(counts) <= {"100", "111"}
    assert counts

    local_counts = offloader.sample(circuit, shots=16, local=True, simulator=True)
    assert sum(local_counts.values()) == 16
    assert set(local_counts) <= {"100", "111"}


def test_sampler_cli_help(script_runner: ScriptRunner) -> None:
    """Test running the sampler CLI with the --help flag."""
    result = script_runner.run(["iqm-sampler", "--help"])
    assert result.success
    assert "Sample a serialized QPY circuit" in result.stdout
