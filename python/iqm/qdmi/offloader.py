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

# ruff: noqa: S603
"""Offload Qiskit workloads (sampling and estimation) using Slurm."""

from __future__ import annotations

import base64
import contextlib
import os
import pickle  # noqa: S403
import subprocess
import uuid
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import TYPE_CHECKING, SupportsInt, cast

_IMPORT_ERROR: ImportError | None = None
try:
    from qiskit import QuantumCircuit, qpy, transpile
    from qiskit_algorithms import VQE
    from qiskit_algorithms.optimizers import L_BFGS_B

    from ._backends import TRANSPILE_OPTIMIZATION_LEVEL, build_estimator, build_sampler
except ImportError as e:
    _IMPORT_ERROR = e

if TYPE_CHECKING:
    from qiskit.primitives.containers.pub_result import PubResult
    from qiskit.quantum_info import SparsePauliOp
    from qiskit_algorithms import VQEResult


def _count_to_int(count: object) -> int:
    """Convert an external count payload to a plain integer.

    Returns:
        The converted count value.
    """
    return int(cast("SupportsInt | str | bytes | bytearray", count))


def normalize_counts(raw_counts: object) -> dict[str, int]:
    """Convert backend count payloads into a plain typed dictionary.

    Returns:
        A normalized mapping from bitstrings to integer counts.
    """
    if isinstance(raw_counts, Mapping):
        return {str(bitstring): _count_to_int(count) for bitstring, count in raw_counts.items()}

    pairs = cast("Iterable[tuple[object, object]]", raw_counts)
    return {str(bitstring): _count_to_int(count) for bitstring, count in pairs}


def extract_counts(pub_result: PubResult) -> dict[str, int]:
    """Extract counts from the first classical register exposed by a primitive result.

    Returns:
        A normalized mapping from measured bitstrings to shot counts.

    Raises:
        RuntimeError: If no classical register with counts is present in the result.
    """
    data = pub_result.data

    if isinstance(data, dict):
        values: Iterable[object] = data.values()
    else:
        keys = getattr(data, "keys", None)
        values = (data[key] for key in keys()) if callable(keys) else ()

    for value in values:
        get_counts = getattr(value, "get_counts", None)
        if callable(get_counts):
            return normalize_counts(get_counts())

    for key in ("meas", "c"):
        with contextlib.suppress(AttributeError, KeyError, TypeError):
            value = data[key]
            get_counts = getattr(value, "get_counts", None)
            if callable(get_counts):
                return normalize_counts(get_counts())

    msg = "Could not find a classical register with counts in the primitive result."
    raise RuntimeError(msg)


def _get_jobs_dir() -> Path:
    """Get the jobs directory path.

    Returns the directory specified by IQM_JOBS_DIR environment variable,
    or defaults to the user's home directory under `.qdmi_jobs`.

    Returns:
        Path to the jobs directory. The directory is not created; callers
        are responsible for creating it.
    """
    env_dir = os.getenv("IQM_JOBS_DIR")
    if env_dir:
        return Path(env_dir)

    return Path.home() / ".qdmi_jobs"


def _new_job_dir() -> Path:
    """Create and return a fresh per-call job directory on the shared jobs filesystem.

    Returns:
        Path to the newly created job directory.
    """
    jobs_dir = _get_jobs_dir()
    jobs_dir.mkdir(parents=True, exist_ok=True)
    job_dir = jobs_dir / uuid.uuid4().hex
    job_dir.mkdir(exist_ok=False)
    return job_dir


def _spank_qc_selection_args(qc_id: str | None, qc_alias: str | None) -> list[str]:
    """Build `srun` options for an explicit per-job quantum computer selection.

    Unlike backend credentials (`IQM_BASE_URL`/`IQM_TOKENS_FILE`), which reach
    the job purely through the environment -- either plain Slurm propagation
    from the submitting shell, or the QDMI-on-IQM SPANK plugin's own
    plugstack.conf.d defaults on the `quantum` partition -- QC selection is a
    per-call choice. It is passed as a `--iqm-qc-id`/`--iqm-qc-alias` option on
    `srun` itself, which the SPANK plugin resolves into the job's
    `IQM_QC_ID`/`IQM_QC_ALIAS` environment variable.

    Returns:
        List of `srun` options for the requested quantum computer selection.
    """
    args = []
    if qc_id:
        args.append(f"--iqm-qc-id={qc_id}")
    if qc_alias:
        args.append(f"--iqm-qc-alias={qc_alias}")
    return args


def _run_srun(command: list[str], job_dir: Path) -> subprocess.CompletedProcess[bytes]:
    """Run *command* via `srun` and return the completed process.

    On failure, *job_dir* (containing the serialized inputs) is intentionally
    left on disk rather than cleaned up, so its path is included in the raised
    error to make it discoverable for debugging.

    Returns:
        The completed process, with captured stdout/stderr.

    Raises:
        RuntimeError: If the Slurm job returns a non-zero exit code.
    """
    process = subprocess.run(command, capture_output=True, check=False)
    if process.returncode != 0:
        stderr = process.stderr.decode().strip()
        msg = f"Error while submitting job to Slurm: {stderr} (job inputs kept at {job_dir} for debugging)"
        raise RuntimeError(msg)
    return process


def _decode_payload(process: subprocess.CompletedProcess[bytes]) -> bytes:
    """Extract and base64-decode the job's stdout payload.

    Returns:
        The decoded, still-pickled result payload.

    Raises:
        RuntimeError: If the job produced no output.
    """
    stdout = process.stdout.decode().strip()
    if not stdout:
        msg = "No output from the job."
        raise RuntimeError(msg)
    return base64.b64decode(stdout.encode())


def _load_pickled_result(payload: bytes) -> object:
    """Unpickle a decoded job result payload.

    Returns:
        The unpickled result object.

    Raises:
        RuntimeError: If the payload cannot be unpickled.
    """
    try:
        return pickle.loads(payload)  # noqa: S301
    except Exception as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e


def sample(
    qc: QuantumCircuit,
    shots: int = 1024,
    *,
    local: bool = False,
    simulator: bool = False,
    timeout: float | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
) -> dict[str, int]:
    """Sample from a quantum circuit.

    When `local=False` (default), serializes the given circuit to QPY format
    and submits it to the Slurm workload manager using the `srun` command.
    After completion, the counts are parsed and returned as a dictionary.

    When `local=True`, runs the circuit locally using the MQT Core DDSIM simulator backend.

    Args:
        qc: The quantum circuit to run.
        shots: The number of shots to run. Default is 1024.
        local: If True, run the job locally using the built-in QDMI simulator backend.
            If False (default), offload to Slurm.
        simulator: If True, run the job on the simulator instead of the quantum computer.
            Only used when `local=False`.
        timeout: The timeout passed to the IQM Backend in seconds.
            Only used when `local=False`.
        qc_id: If given, passed as `--iqm-qc-id` to `srun`, which the QDMI-on-IQM
            SPANK plugin resolves into the `IQM_QC_ID` job environment variable.
            Only used when `local=False`.
        qc_alias: If given, passed as `--iqm-qc-alias` to `srun`, which the
            QDMI-on-IQM SPANK plugin resolves into the `IQM_QC_ALIAS` job
            environment variable. Only used when `local=False`.

    Returns:
        A dictionary of measurement counts.

    Raises:
        ImportError: If Qiskit or the QDMI backend plugins are not installed.
        RuntimeError: If there is an error while submitting the job to Slurm or parsing the output.
    """  # noqa: DOC502
    if _IMPORT_ERROR is not None:
        msg = (
            "Failed to import Qiskit and QDMI backend plugins. "
            "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
        )
        raise ImportError(msg) from _IMPORT_ERROR
    if local:
        backend, sampler = build_sampler(simulator=simulator)
        qc_for_execution = transpile(qc, backend, optimization_level=TRANSPILE_OPTIMIZATION_LEVEL)
        job = sampler.run([(qc_for_execution,)], shots=shots)
        first_pub = next(iter(job.result()))
        return extract_counts(first_pub)

    # Make sure the `jobs` directory exists on the shared filesystem
    job_dir = _new_job_dir()

    # Serialize the circuit to QPY format
    qc_path = job_dir / "qc.qpy"
    with qc_path.open("wb") as f:
        qpy.dump(qc, f)

    # Run the job using srun, which will return the counts upon completion
    # This call is blocking and will wait for the job to finish before returning.
    job_name = "sample_sim" if simulator else "sample_qc"
    command = [
        "srun",
        f"--job-name={job_name}",
        "--nodes=1",
        "--partition=quantum",
        *_spank_qc_selection_args(qc_id, qc_alias),
        "iqm-sampler",
        str(qc_path.absolute()),
        "--shots",
        str(shots),
    ]
    if simulator:
        command.append("--simulator")
    if timeout:
        command.extend(["--timeout", str(timeout)])

    process = _run_srun(command, job_dir)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    payload = _decode_payload(process)
    primitive_result = cast("Iterable[PubResult]", _load_pickled_result(payload))
    first_pub = next(iter(primitive_result))
    return extract_counts(first_pub)


def estimate(
    ansatz: QuantumCircuit,
    operator: SparsePauliOp,
    maxiter: int = 80,
    *,
    local: bool = False,
    simulator: bool = False,
    timeout: float | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
) -> VQEResult:
    """Estimate the optimal parameters for a given ansatz circuit and operator.

    When `local=False` (default), serializes the given ansatz and operator to
    QPY/pickle format and submits them to the Slurm workload manager using the
    `srun` command. After completion, the VQE result is parsed and returned.

    When `local=True`, runs the VQE algorithm locally using either the MQT Core
    DDSIM simulator backend or the packaged IQM backend.

    The returned result has the same semantics as calling
    `VQE(...).compute_minimum_eigenvalue(...)` directly against the regular
    (non-offloaded) estimator.

    Args:
        ansatz: The ansatz circuit to run.
        operator: The operator to run.
        maxiter: The maximum number of iterations for the optimization.
            Default is 80.
        local: If True, run the job locally using the built-in QDMI simulator backend.
            If False (default), offload to Slurm.
        simulator: If True, run the job on the simulator instead of the quantum computer.
            Only used when `local=False`.
        timeout: The timeout passed to the IQM Backend in seconds.
            Only used when `local=False`.
        qc_id: If given, passed as `--iqm-qc-id` to `srun`, which the QDMI-on-IQM
            SPANK plugin resolves into the `IQM_QC_ID` job environment variable.
            Only used when `local=False`.
        qc_alias: If given, passed as `--iqm-qc-alias` to `srun`, which the
            QDMI-on-IQM SPANK plugin resolves into the `IQM_QC_ALIAS` job
            environment variable. Only used when `local=False`.

    Returns:
        The VQE result, including the optimal parameters and eigenvalue.

    Raises:
        ImportError: If Qiskit or the QDMI backend plugins are not installed.
        RuntimeError: If there is an error while submitting the job to Slurm or parsing the output.
    """  # noqa: DOC502
    if _IMPORT_ERROR is not None:
        msg = (
            "Failed to import Qiskit and QDMI backend plugins. "
            "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
        )
        raise ImportError(msg) from _IMPORT_ERROR
    if local:
        _, estimator = build_estimator(simulator=simulator)
        vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=maxiter))
        return vqe.compute_minimum_eigenvalue(operator=operator)

    # Make sure the `jobs` directory exists on the shared filesystem
    job_dir = _new_job_dir()

    # Serialize the circuit to QPY format
    qc_path = job_dir / "ansatz.qpy"
    with qc_path.open("wb") as f:
        qpy.dump(ansatz, f)

    # Serialize the operator using pickle
    operator_path = job_dir / "operator.pkl"
    with operator_path.open("wb") as f:
        pickle.dump(operator, f)

    # Run the job using srun, which will return the list of optimal parameters upon completion
    job_name = "estim_sim" if simulator else "estim_qc"
    command = [
        "srun",
        f"--job-name={job_name}",
        "--nodes=1",
        "--partition=quantum",
        *_spank_qc_selection_args(qc_id, qc_alias),
        "iqm-estimator",
        str(qc_path.absolute()),
        str(operator_path.absolute()),
        "--maxiter",
        str(maxiter),
    ]
    if simulator:
        command.append("--simulator")
    if timeout:
        command.extend(["--timeout", str(timeout)])

    process = _run_srun(command, job_dir)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    operator_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    payload = _decode_payload(process)
    return cast("VQEResult", _load_pickled_result(payload))
