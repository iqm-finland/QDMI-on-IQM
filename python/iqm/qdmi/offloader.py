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

import numpy as np

_IMPORT_ERROR: ImportError | None = None
try:
    from mqt.core.plugins.qiskit.estimator import QDMIEstimator
    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from mqt.core.plugins.qiskit.sampler import QDMISampler
    from qiskit import QuantumCircuit, qpy, transpile
    from qiskit_algorithms import VQE
    from qiskit_algorithms.optimizers import L_BFGS_B

    from iqm.qdmi.qiskit import IQMBackend
except ImportError as e:
    _IMPORT_ERROR = e

if TYPE_CHECKING:
    from qiskit.primitives.containers.pub_result import PubResult
    from qiskit.quantum_info import SparsePauliOp


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


def _new_job_id() -> str:
    """Return a unique ID for a single offloading call."""
    return uuid.uuid4().hex


def _make_job_dir(jobs_dir: Path, job_id: str) -> Path:
    """Create and return a per-call job directory under *jobs_dir*.

    Args:
        jobs_dir: The base directory for job storage.
        job_id: Unique identifier for the job.

    Returns:
        Path to the newly created job directory.
    """
    job_dir = jobs_dir / job_id
    job_dir.mkdir(parents=True, exist_ok=False)
    return job_dir


def _get_jobs_dir() -> Path:
    """Get the jobs directory path.

    Returns the directory specified by IQM_JOBS_DIR environment variable,
    or defaults to the user's home directory under `.qdmi_jobs`.

    Returns:
        Path to the jobs directory.
    """
    # Check if environment variable is set
    env_dir = os.getenv("IQM_JOBS_DIR")
    if env_dir:
        return Path(env_dir)

    # Default to user's home directory
    default_dir = Path.home() / ".qdmi_jobs"
    default_dir.mkdir(parents=True, exist_ok=True)
    return default_dir


def _build_worker_backend_args() -> list[str]:
    """Build worker CLI backend options from environment variables.

    Reads the canonical IQM environment contract and converts it to
    worker CLI arguments so remote jobs can reconstruct the same backend
    configuration.

    Returns:
        List of worker CLI options for IQM configuration.
    """
    args = []

    if base_url := os.getenv("IQM_BASE_URL"):
        args.extend(["--base-url", base_url])

    if tokens_file := os.getenv("IQM_TOKENS_FILE"):
        args.extend(["--tokens-file", tokens_file])

    if qc_id := os.getenv("IQM_QC_ID"):
        args.extend(["--qc-id", qc_id])
    if qc_alias := os.getenv("IQM_QC_ALIAS"):
        args.extend(["--qc-alias", qc_alias])

    return args


def sample(
    qc: QuantumCircuit,
    shots: int = 1024,
    *,
    local: bool = False,
    simulator: bool = False,
    timeout: float | None = None,
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

    Returns:
        A dictionary of measurement counts.

    Raises:
        ImportError: If Qiskit or the QDMI backend plugins are not installed.
        RuntimeError: If there is an error while submitting the job to Slurm or parsing the output.
    """
    if _IMPORT_ERROR is not None:
        msg = (
            "Failed to import Qiskit and QDMI backend plugins. "
            "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
        )
        raise ImportError(msg) from _IMPORT_ERROR
    if local:
        if simulator:
            backend = QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
            qc_for_execution = qc
            sampler = QDMISampler(backend)
        else:
            backend = IQMBackend()
            qc_for_execution = transpile(qc, backend, optimization_level=3)
            sampler = backend.sampler()

        job = sampler.run([(qc_for_execution,)], shots=shots)
        first_pub = next(iter(job.result()))
        return extract_counts(first_pub)

    # Make sure the `jobs` directory exists on the shared filesystem
    jobs_dir = _get_jobs_dir()
    jobs_dir.mkdir(parents=True, exist_ok=True)

    job_id = _new_job_id()
    job_dir = _make_job_dir(jobs_dir, job_id)

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
        "iqm-sampler",
        str(qc_path.absolute()),
        "--shots",
        str(shots),
        *_build_worker_backend_args(),
    ]
    if simulator:
        command.append("--simulator")
    if timeout:
        command.extend(["--timeout", str(timeout)])

    process = subprocess.run(
        command,
        capture_output=True,
        check=False,
    )

    # Check for errors before deleting the file
    if process.returncode != 0:
        # Keep the file for debugging
        stderr = process.stderr.decode().strip()
        msg = f"Error while submitting job to Slurm: {stderr}"
        raise RuntimeError(msg)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    # Parse the counts from the output
    stdout = process.stdout.decode().strip()
    if not stdout:
        msg = "No output from the job."
        raise RuntimeError(msg)
    try:
        decoded = base64.b64decode(stdout.encode())
        primitive_result = pickle.loads(decoded)  # noqa: S301
        first_pub = next(iter(primitive_result))
        counts = extract_counts(first_pub)
    except Exception as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e
    return counts


def estimate(
    ansatz: QuantumCircuit,
    operator: SparsePauliOp,
    maxiter: int = 80,
    *,
    local: bool = False,
    simulator: bool = False,
    timeout: float | None = None,
) -> list[np.float64]:
    """Estimate the optimal parameters for a given ansatz circuit and operator.

    When `local=False` (default), serializes the given ansatz and operator to
    QPY/pickle format and submits them to the Slurm workload manager using the
    `srun` command. After completion, the optimal parameters are parsed and
    returned as a list of floats.

    When `local=True`, runs the VQE algorithm locally using either the MQT Core
    DDSIM simulator backend or the packaged IQM backend.

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

    Returns:
        A list of optimal parameters.

    Raises:
        ImportError: If Qiskit or the QDMI backend plugins are not installed.
        RuntimeError: If there is an error while submitting the job to Slurm or parsing the output.
        ValueError: If the VQE result has no optimal parameters.
    """
    if _IMPORT_ERROR is not None:
        msg = (
            "Failed to import Qiskit and QDMI backend plugins. "
            "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
        )
        raise ImportError(msg) from _IMPORT_ERROR
    if local:
        estimator = (
            QDMIEstimator(QDMIProvider().get_backend("MQT Core DDSIM QDMI Device"))
            if simulator
            else IQMBackend().estimator()
        )

        vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=maxiter))
        result = vqe.compute_minimum_eigenvalue(operator=operator)
        optimal_parameters = result.optimal_parameters
        if optimal_parameters is None:
            msg = "VQE result has no optimal parameters."
            raise ValueError(msg)
        return list(optimal_parameters.values())

    # Make sure the `jobs` directory exists on the shared filesystem
    jobs_dir = _get_jobs_dir()
    jobs_dir.mkdir(parents=True, exist_ok=True)

    job_id = _new_job_id()
    job_dir = _make_job_dir(jobs_dir, job_id)

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
        "iqm-estimator",
        str(qc_path.absolute()),
        str(operator_path.absolute()),
        "--maxiter",
        str(maxiter),
        *_build_worker_backend_args(),
    ]
    if simulator:
        command.append("--simulator")
    if timeout:
        command.extend(["--timeout", str(timeout)])

    process = subprocess.run(
        command,
        capture_output=True,
        check=False,
    )

    if process.returncode != 0:
        stderr = process.stderr.decode().strip()
        msg = f"Error while submitting job to Slurm: {stderr}"
        raise RuntimeError(msg)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    operator_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    # Parse the optimal parameters from the output
    stdout = process.stdout.decode().strip()
    if not stdout:
        msg = "No output from the job."
        raise RuntimeError(msg)

    # Convert the output to a list of floats
    try:
        decoded = base64.b64decode(stdout.encode())
        vqe_result = pickle.loads(decoded)  # noqa: S301
        optimal_params = [np.float64(val) for val in vqe_result.optimal_parameters.values()]
    except Exception as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e

    return optimal_params
