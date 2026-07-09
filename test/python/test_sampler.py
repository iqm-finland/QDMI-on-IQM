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
import pickle  # noqa: S403
from typing import TYPE_CHECKING

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

    decoded = base64.b64decode(result.stdout.strip().encode())
    res = pickle.loads(decoded)  # noqa: S301
    first_pub = next(iter(res))
    counts = first_pub.data.meas.get_counts()
    assert sum(counts.values()) == 256
    assert set(counts) <= {"00", "11"}
    assert counts


def test_sampler_cli_help(script_runner: ScriptRunner) -> None:
    """Test running the sampler CLI with the --help flag."""
    result = script_runner.run(["iqm-sampler", "--help"])
    assert result.success
    assert "Sample a serialized QPY circuit" in result.stdout
