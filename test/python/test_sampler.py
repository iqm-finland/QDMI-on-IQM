# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
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

import ast
import sys
from subprocess import check_output
from typing import TYPE_CHECKING

import pytest
from qiskit import QuantumCircuit, qpy

if TYPE_CHECKING:
    from pathlib import Path

    from pytest_console_scripts import ScriptRunner


def test_sampler_cli_simulator(tmp_path: Path, script_runner: ScriptRunner) -> None:
    """The sampler CLI should execute a serialized circuit on the simulator."""
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure_all()

    circuit_path = tmp_path / "bell.qpy"
    with circuit_path.open("wb") as file_obj:
        qpy.dump(circuit, file_obj)

    result = script_runner.run(["iqm-sampler", str(circuit_path), "--shots", "256", "--simulator"])
    assert result.success

    counts = ast.literal_eval(result.stdout.strip())
    assert sum(counts.values()) == 256
    assert set(counts) <= {"00", "11"}
    assert counts


@pytest.mark.skipif(sys.platform.startswith("win"), reason="The subprocess calls do not work properly on Windows.")
def test_sampler_execute_module() -> None:
    """Test running the sampler CLI by executing the iqm.qdmi.sampler module."""
    output = check_output([sys.executable, "-m", "iqm.qdmi.sampler", "--help"])
    assert "Sample a serialized QPY circuit" in output.decode()
