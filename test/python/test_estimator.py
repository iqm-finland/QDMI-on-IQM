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

"""Tests for the QDMI-on-IQM estimator CLI script."""

from __future__ import annotations

import base64
import math
import pickle  # noqa: S403
from typing import TYPE_CHECKING

from qiskit import QuantumCircuit, qpy
from qiskit.circuit import Parameter
from qiskit.quantum_info import SparsePauliOp

if TYPE_CHECKING:
    from pathlib import Path

    from pytest_console_scripts import ScriptRunner


def test_estimator_cli_simulator(tmp_path: Path, script_runner: ScriptRunner) -> None:
    """The estimator CLI should execute a serialized VQE input on the simulator."""
    theta = Parameter("theta")
    ansatz = QuantumCircuit(1)
    ansatz.ry(theta, 0)
    observable = SparsePauliOp.from_list([("Z", 1.0)])

    ansatz_path = tmp_path / "ansatz.qpy"
    with ansatz_path.open("wb") as file_obj:
        qpy.dump(ansatz, file_obj)

    observable_path = tmp_path / "observable.pkl"
    with observable_path.open("wb") as file_obj:
        pickle.dump(observable, file_obj)

    result = script_runner.run([
        "iqm-estimator",
        str(ansatz_path),
        str(observable_path),
        "--maxiter",
        "5",
        "--simulator",
    ])
    assert result.success

    decoded = base64.b64decode(result.stdout.strip().encode())
    res = pickle.loads(decoded)  # noqa: S301
    assert hasattr(res, "optimal_parameters")
    assert hasattr(res, "eigenvalue")
    params = list(res.optimal_parameters.values())
    assert len(params) == 1
    assert math.isfinite(float(params[0]))


def test_estimator_cli_help(script_runner: ScriptRunner) -> None:
    """Test running the estimator CLI with the --help flag."""
    result = script_runner.run(["iqm-estimator", "--help"])
    assert result.success
    assert "Estimate VQE parameters" in result.stdout
