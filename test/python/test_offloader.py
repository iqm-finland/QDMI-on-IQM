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
import pickle  # ruff:ignore[suspicious-pickle-import]
import re
import subprocess
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from qiskit import QuantumCircuit
from qiskit.circuit import Parameter, ParameterVector
from qiskit.primitives.containers import BitArray, DataBin, PrimitiveResult, SamplerPubResult
from qiskit.quantum_info import SparsePauliOp
from qiskit_algorithms import VQEResult

from iqm.qdmi import offloader

if TYPE_CHECKING:
    from collections.abc import Callable

_SAMPLE_RESULT = PrimitiveResult([SamplerPubResult(DataBin(meas=BitArray.from_counts({"0": 1})))])
_VQE_RESULT = VQEResult()
_VQE_RESULT.optimal_parameters = {"theta": 0.125}

#: The stdout a worker produces for a sampling job returning a single `0` shot.
SAMPLE_STDOUT = base64.b64encode(pickle.dumps(_SAMPLE_RESULT))

#: The stdout a worker produces for an estimation job converging on `theta`.
ESTIMATE_STDOUT = base64.b64encode(pickle.dumps(_VQE_RESULT))


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
        stdout = SAMPLE_STDOUT
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
        stdout = ESTIMATE_STDOUT
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


def test_sample_slurm_uses_spank_qc_alias_and_no_cli_credentials(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """The Slurm path must never put credentials on the `srun` command line.

    Backend credentials (`IQM_BASE_URL`/`IQM_TOKENS_FILE`) reach the job
    purely through the environment -- either plain Slurm propagation or the
    QDMI-on-IQM SPANK plugin's own injection -- never as CLI arguments. Only
    the explicit `qc_alias` selection is forwarded, as a SPANK `--iqm-*`
    option on `srun` itself (not a worker CLI flag).
    """
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = SAMPLE_STDOUT
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
    monkeypatch.setenv("IQM_BASE_URL", "https://resonance.example")
    monkeypatch.setenv("IQM_TOKENS_FILE", "tokens_path")
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    counts = offloader.sample(circuit, shots=7, local=False, simulator=True, qc_alias="emerald:mock")

    worker_index = captured_command.index("iqm-sampler")
    worker_command = captured_command[worker_index : worker_index + 4]
    assert counts == {"0": 1}
    assert "https://resonance.example" not in captured_command
    assert "tokens_path" not in captured_command
    for flag in ("--base-url", "--tokens-file", "--token", "--qc-alias"):
        assert flag not in captured_command
    assert "--iqm-qc-alias=emerald:mock" in captured_command
    assert captured_command.index("--iqm-qc-alias=emerald:mock") < worker_index
    assert worker_command[0] == "iqm-sampler"
    assert Path(worker_command[1]).name == "qc.qpy"
    assert worker_command[2] == "--shots"
    assert worker_command[3] == "7"


def test_estimate_slurm_uses_spank_qc_id_and_no_cli_credentials(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Mirrors `test_sample_slurm_uses_spank_qc_alias_and_no_cli_credentials` for `estimate()`."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = ESTIMATE_STDOUT
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
    monkeypatch.setenv("IQM_BASE_URL", "https://resonance.example")
    monkeypatch.setenv("IQM_TOKENS_FILE", "tokens_path")
    monkeypatch.setattr(subprocess, "run", fake_run)

    ansatz = QuantumCircuit(1)
    operator = SparsePauliOp.from_list([("Z", 1.0)])

    qc_id = "12345678-1234-1234-1234-123456789abc"
    result = offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True, qc_id=qc_id)

    worker_index = captured_command.index("iqm-estimator")
    worker_command = captured_command[worker_index : worker_index + 5]
    assert result.optimal_parameters == {"theta": 0.125}
    assert "https://resonance.example" not in captured_command
    assert "tokens_path" not in captured_command
    for flag in ("--base-url", "--tokens-file", "--token", "--qc-id", "--qc-alias"):
        assert flag not in captured_command
    assert f"--iqm-qc-id={qc_id}" in captured_command
    assert captured_command.index(f"--iqm-qc-id={qc_id}") < worker_index
    assert worker_command[0] == "iqm-estimator"
    assert Path(worker_command[1]).name == "ansatz.qpy"
    assert Path(worker_command[2]).name == "operator.pkl"
    assert worker_command[3] == "--maxiter"
    assert worker_command[4] == "3"


def test_sample_slurm_forwards_licenses(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """The `licenses` kwarg is forwarded verbatim as `--licenses` on `srun`, ahead of the worker command."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = SAMPLE_STDOUT
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

    counts = offloader.sample(
        circuit, shots=7, local=False, simulator=True, qc_alias="emerald:mock", licenses="iqm_qc_emerald_mock:1"
    )

    worker_index = captured_command.index("iqm-sampler")
    assert counts == {"0": 1}
    assert "--licenses=iqm_qc_emerald_mock:1" in captured_command
    assert captured_command.index("--licenses=iqm_qc_emerald_mock:1") < worker_index


def test_sample_slurm_omits_licenses_by_default(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Without a `licenses` kwarg, `--licenses` is not added to the `srun` command."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = SAMPLE_STDOUT
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

    offloader.sample(circuit, shots=7, local=False, simulator=True)

    assert not any(arg.startswith("--licenses") for arg in captured_command)


def test_estimate_slurm_forwards_licenses(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Mirrors `test_sample_slurm_forwards_licenses` for `estimate()`."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = ESTIMATE_STDOUT
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

    result = offloader.estimate(
        ansatz,
        operator,
        maxiter=3,
        local=False,
        simulator=True,
        qc_alias="emerald:mock",
        licenses="iqm_qc_emerald_mock:1",
    )

    worker_index = captured_command.index("iqm-estimator")
    assert result.optimal_parameters == {"theta": 0.125}
    assert "--licenses=iqm_qc_emerald_mock:1" in captured_command
    assert captured_command.index("--licenses=iqm_qc_emerald_mock:1") < worker_index


@pytest.mark.parametrize(
    ("partition", "partition_env", "expected"),
    [
        (None, None, "--partition=quantum"),
        (None, "qc-nodes", "--partition=qc-nodes"),
        ("qc-nodes", "ignored", "--partition=qc-nodes"),
    ],
)
def test_sample_slurm_resolves_partition(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    partition: str | None,
    partition_env: str | None,
    expected: str,
) -> None:
    """An explicit `partition` wins over `IQM_SLURM_PARTITION`, which wins over the `quantum` default."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = SAMPLE_STDOUT
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
    if partition_env is None:
        monkeypatch.delenv("IQM_SLURM_PARTITION", raising=False)
    else:
        monkeypatch.setenv("IQM_SLURM_PARTITION", partition_env)
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    offloader.sample(circuit, shots=7, local=False, simulator=True, partition=partition)

    assert expected in captured_command
    assert captured_command.index(expected) < captured_command.index("iqm-sampler")


def test_sample_slurm_requests_nodes(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """The `nodes` kwarg sizes the allocation while the worker stays a single task."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = SAMPLE_STDOUT
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

    # Both options only reach Slurm if they precede the worker on the command
    # line; after it they would be the worker's own arguments.
    offloader.sample(circuit, shots=7, local=False, simulator=True)
    worker_index = captured_command.index("iqm-sampler")
    assert captured_command.index("--nodes=1") < worker_index
    assert captured_command.index("--ntasks=1") < worker_index

    offloader.sample(circuit, shots=7, local=False, simulator=True, nodes=4)
    worker_index = captured_command.index("iqm-sampler")
    assert captured_command.index("--nodes=4") < worker_index
    assert captured_command.index("--ntasks=1") < worker_index


@pytest.mark.parametrize(
    ("partition", "partition_env", "expected"),
    [
        (None, None, "--partition=quantum"),
        (None, "qc-nodes", "--partition=qc-nodes"),
        ("qc-nodes", "ignored", "--partition=qc-nodes"),
    ],
)
def test_estimate_slurm_resolves_partition(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
    partition: str | None,
    partition_env: str | None,
    expected: str,
) -> None:
    """`estimate()` walks the same resolution order as `test_sample_slurm_resolves_partition`."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = ESTIMATE_STDOUT
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
    if partition_env is None:
        monkeypatch.delenv("IQM_SLURM_PARTITION", raising=False)
    else:
        monkeypatch.setenv("IQM_SLURM_PARTITION", partition_env)
    monkeypatch.setattr(subprocess, "run", fake_run)

    ansatz = QuantumCircuit(1)
    operator = SparsePauliOp.from_list([("Z", 1.0)])

    offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True, partition=partition)

    assert expected in captured_command
    assert captured_command.index(expected) < captured_command.index("iqm-estimator")


def test_estimate_slurm_requests_nodes(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Mirrors `test_sample_slurm_requests_nodes` for `estimate()`."""
    captured_command: list[str] = []

    class FakeCompletedProcess:
        returncode = 0
        stdout = ESTIMATE_STDOUT
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

    offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True)
    worker_index = captured_command.index("iqm-estimator")
    assert captured_command.index("--nodes=1") < worker_index
    assert captured_command.index("--ntasks=1") < worker_index

    offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True, nodes=2)
    worker_index = captured_command.index("iqm-estimator")
    assert captured_command.index("--nodes=2") < worker_index
    assert captured_command.index("--ntasks=1") < worker_index


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
        offloader._first_pub([])  # ruff:ignore[private-member-access]


_EXECUTED_MARKERS: list[str] = []


def _record_execution(marker: str) -> str:
    """Record that a result payload got this module to run code of its choosing.

    Returns:
        The marker it was called with.
    """
    _EXECUTED_MARKERS.append(marker)
    return marker


class _CallingPayload:
    """A result payload that calls a function of its own choosing while being loaded."""

    def __reduce__(self) -> tuple[Callable[[str], str], tuple[str]]:
        """Reduce to the call the loading process is asked to make.

        Returns:
            The callable and its arguments.
        """
        return (_record_execution, ("executed",))


class _AttributeWalkingPayload:
    """A result payload that tries to walk out of the result types through `getattr`."""

    def __reduce__(self) -> tuple[Callable[[object, str], object], tuple[object, str]]:
        """Reduce to an attribute lookup reaching a method that writes a file of its choosing.

        Returns:
            The callable and its arguments.
        """
        return (getattr, (QuantumCircuit, "draw"))


def _global_payload(module: str, name: str) -> bytes:
    """Build a payload whose only content is a reference to `module.name`.

    Returns:
        The pickled stream.
    """
    parts = b"".join(pickle.SHORT_BINUNICODE + bytes([len(raw)]) + raw for raw in (module.encode(), name.encode()))
    return pickle.PROTO + b"\x04" + parts + pickle.STACK_GLOBAL + pickle.STOP


@pytest.mark.parametrize(
    ("module", "name"),
    [
        # A dotted name would walk attributes out of the module, into what it imported.
        ("qiskit.circuit.quantumcircuit", "multiprocessing.Process"),
        # Qiskit's C API module wraps its native library rather than a result type.
        ("qiskit.capi._ctypes", "QkCircuit"),
        # A module-level Qiskit function is called with the payload's arguments.
        ("qiskit.qasm2", "dump"),
    ],
)
def test_load_pickled_result_refuses_globals_outside_the_result_types(module: str, name: str) -> None:
    """Reaching past the result types within an allowed module is refused, not resolved."""
    with pytest.raises(RuntimeError, match="disallowed class"):
        offloader._load_pickled_result(_global_payload(module, name))  # ruff:ignore[private-member-access]


@pytest.mark.parametrize(
    ("payload", "message"),
    [(_CallingPayload(), "disallowed class"), (_AttributeWalkingPayload(), "disallowed attribute")],
)
def test_sample_slurm_refuses_payload_outside_the_result_types(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, payload: object, message: str
) -> None:
    """A worker payload naming anything but the result types is refused rather than run."""

    class FakeCompletedProcess:
        returncode = 0
        stdout = base64.b64encode(pickle.dumps(payload))
        stderr = b""

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> FakeCompletedProcess:
        assert command
        assert capture_output is True
        assert check is False
        assert timeout is None
        return FakeCompletedProcess()

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    circuit = QuantumCircuit(1)
    circuit.measure_all()

    with pytest.raises(RuntimeError, match=message):
        offloader.sample(circuit, shots=7, local=False, simulator=True)

    assert _EXECUTED_MARKERS == []


def test_estimate_slurm_accepts_a_genuine_vqe_result(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """A real `VQEResult`, circuit and NumPy arrays included, still crosses the Slurm boundary intact."""
    theta = ParameterVector("theta", 2)
    ansatz = QuantumCircuit(2)
    ansatz.ry(theta[0], 0)
    ansatz.cx(0, 1)
    ansatz.ry(theta[1], 1)
    operator = SparsePauliOp.from_list([("ZZ", 1.0)])
    expected = offloader.estimate(ansatz, operator, maxiter=3, local=True, simulator=True)

    class FakeCompletedProcess:
        returncode = 0
        stdout = base64.b64encode(pickle.dumps(expected))
        stderr = b""

    def fake_run(
        command: list[str], *, capture_output: bool, check: bool, timeout: float | None
    ) -> FakeCompletedProcess:
        assert command
        assert capture_output is True
        assert check is False
        assert timeout is None
        return FakeCompletedProcess()

    monkeypatch.setenv("IQM_JOBS_DIR", str(tmp_path))
    monkeypatch.setattr(subprocess, "run", fake_run)

    result = offloader.estimate(ansatz, operator, maxiter=3, local=False, simulator=True)

    assert result.optimal_parameters == expected.optimal_parameters
    assert result.optimal_circuit == expected.optimal_circuit
    assert result.optimal_value == expected.optimal_value
