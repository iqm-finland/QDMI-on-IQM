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

"""Tests for the Slurm offloader module."""

from __future__ import annotations

import base64
import math
import pickle  # noqa: S403
import re
import subprocess
from typing import TYPE_CHECKING

import pytest
from qiskit import QuantumCircuit
from qiskit.circuit import Parameter
from qiskit.quantum_info import SparsePauliOp

from iqm.qdmi import offloader

if TYPE_CHECKING:
    from pathlib import Path


class FakeBitArray:
    """Mock for Qiskit's BitArray."""

    def __init__(self, counts: dict[str, int]) -> None:
        """Initialize counts."""
        self.counts = counts

    def get_counts(self) -> dict[str, int]:
        """Return counts."""
        return self.counts


class FakePubResult:
    """Mock for PubResult."""

    def __init__(self, data: dict[str, FakeBitArray]) -> None:
        """Initialize data."""
        self.data = data
        self.metadata: dict[str, object] = {}


class FakeVQEResult:
    """Mock for VQEResult."""

    def __init__(self, optimal_parameters: dict[str, float]) -> None:
        """Initialize optimal parameters."""
        self.optimal_parameters = optimal_parameters


def test_sample_local_simulator() -> None:
    """Test sampling locally using the DDSIM simulator."""
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure_all()

    counts = offloader.sample(circuit, shots=128, local=True, simulator=True)
    assert sum(counts.values()) == 128
    assert set(counts) <= {"00", "11"}
    assert counts


def test_estimate_local_simulator() -> None:
    """Test estimation locally using the DDSIM simulator."""
    theta = Parameter("theta")
    ansatz = QuantumCircuit(1)
    ansatz.ry(theta, 0)
    operator = SparsePauliOp.from_list([("Z", 1.0)])

    result = offloader.estimate(ansatz, operator, maxiter=5, local=True, simulator=True)
    optimal_parameters = result.optimal_parameters
    assert optimal_parameters is not None
    params = list(optimal_parameters.values())
    assert len(params) == 1
    assert math.isfinite(float(params[0]))


def test_sample_slurm_mock(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Test sampling via Slurm offloading using a mocked subprocess."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = base64.b64encode(pickle.dumps([FakePubResult({"meas": FakeBitArray({"0": 1})})]))
        stderr = b""

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> FakeCompletedProcess:
        assert capture_output is True
        assert check is False
        assert timeout is None
        captured_command[:] = command
        return FakeCompletedProcess()

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    counts = offloader.sample(circuit, shots=7, local=False, simulator=True)

    assert counts == {"0": 1}
    assert "srun" in captured_command
    assert "iqm-sampler" in captured_command
    assert "--shots" in captured_command
    assert "7" in captured_command
    # Backend configuration (base URL, tokens, etc.) is not passed explicitly:
    # it is inherited by the job's environment (e.g. via the Slurm SPANK plugin).
    assert "--base-url" not in captured_command


def test_estimate_slurm_mock(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Test estimation via Slurm offloading using a mocked subprocess."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = base64.b64encode(pickle.dumps(FakeVQEResult({"theta": 0.125})))
        stderr = b""

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> FakeCompletedProcess:
        assert capture_output is True
        assert check is False
        assert timeout is None
        captured_command[:] = command
        return FakeCompletedProcess()

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    ansatz = QuantumCircuit(1)
    operator = SparsePauliOp.from_list([("Z", 1.0)])

    result = offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True)

    assert result.optimal_parameters == {"theta": 0.125}
    assert "srun" in captured_command
    assert "iqm-estimator" in captured_command
    assert "--maxiter" in captured_command
    assert "3" in captured_command
    # Backend configuration (base URL, tokens, etc.) is not passed explicitly:
    # it is inherited by the job's environment (e.g. via the Slurm SPANK plugin).
    assert "--base-url" not in captured_command


def test_sample_slurm_failure_keeps_job_dir_for_debugging(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """A failed Slurm job leaves its input files in place and reports where to find them."""

    class FakeCompletedProcess:
        returncode = 1
        stdout = b""
        stderr = b"srun: error: something went wrong"

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> FakeCompletedProcess:
        assert capture_output is True
        assert check is False
        assert timeout is None
        assert command
        return FakeCompletedProcess()

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    with pytest.raises(RuntimeError, match=re.escape(str(tmp_path))):
        offloader.sample(circuit, shots=7, local=False, simulator=True)

    # The job directory and its serialized circuit must survive the failure for debugging.
    job_dirs = list(tmp_path.iterdir())
    assert len(job_dirs) == 1
    assert (job_dirs[0] / "qc.qpy").exists()


def test_sample_slurm_timeout_raises_runtime_error(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """A Slurm job that exceeds the given timeout raises a clear RuntimeError, not a bare subprocess error."""

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> subprocess.CompletedProcess[bytes]:
        assert capture_output is True
        assert check is False
        assert timeout == 5
        raise subprocess.TimeoutExpired(cmd=command, timeout=timeout)

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    with pytest.raises(RuntimeError, match="timed out"):
        offloader.sample(circuit, shots=7, local=False, simulator=True, timeout=5)


def test_first_pub_raises_on_empty_result() -> None:
    """An empty primitive result raises a clear RuntimeError instead of a bare StopIteration."""
    with pytest.raises(RuntimeError, match="no pubs"):
        offloader._first_pub([])  # noqa: SLF001
