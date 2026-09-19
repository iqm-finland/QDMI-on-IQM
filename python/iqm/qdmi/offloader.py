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

# ruff:file-ignore[subprocess-without-shell-equals-true]
"""Offload Qiskit workloads (sampling and estimation) using Slurm."""

from __future__ import annotations

import contextlib
import json
import math
import os
import pickle  # ruff:ignore[suspicious-pickle-import]
import subprocess
import uuid
from pathlib import Path
from typing import TYPE_CHECKING, cast

_IMPORT_ERROR: ImportError | None = None
try:
    import numpy as np
    from qiskit import QuantumCircuit, qpy, transpile
    from qiskit_algorithms import VQE, VQEResult
    from qiskit_algorithms.optimizers import L_BFGS_B, OptimizerResult

    from ._backends import TRANSPILE_OPTIMIZATION_LEVEL, build_estimator, build_sampler
except ImportError as e:
    _IMPORT_ERROR = e

if TYPE_CHECKING:
    from collections.abc import Mapping

    from numpy.typing import NDArray
    from qiskit.primitives.containers import BitArray, PrimitiveResult, SamplerPubResult
    from qiskit.quantum_info import SparsePauliOp

_DEFAULT_PARTITION = "quantum"
_DEFAULT_NODES = 1


def _nonnegative_int(value: object) -> int:
    """Validate an integer count from JSON.

    Returns:
        The count unchanged.

    Raises:
        ValueError: If the value is not a nonnegative integer.
    """
    if type(value) is not int or value < 0:
        msg = "Expected a nonnegative JSON integer."
        raise ValueError(msg)
    return value


def extract_counts(primitive_result: PrimitiveResult[SamplerPubResult]) -> dict[str, int]:
    """Extract joint counts from the native sampler's single submitted circuit.

    Returns:
        Joint bitstrings in Qiskit's register order, mapped to shot counts.

    Raises:
        RuntimeError: If the result is empty or has no classical registers with counts.
    """
    if not primitive_result:
        msg = "Primitive result contained no pubs."
        raise RuntimeError(msg)
    try:
        return cast("BitArray", primitive_result[0].join_data()).get_counts()
    except (TypeError, ValueError) as e:
        msg = f"Could not extract measurement counts: {e}"
        raise RuntimeError(msg) from e


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
    plugstack.conf.d defaults on whichever partitions it is configured for --
    QC selection is a per-call choice. It is passed as a `--iqm-qc-id`/`--iqm-qc-alias` option on
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


def _licenses_arg(licenses: str | None) -> list[str]:
    """Build `srun` options for an optional Slurm license request.

    Slurm licenses are a cluster admin's own capacity-limiting mechanism (see
    the SPANK plugin's "Limiting Concurrent Access with Slurm Licenses" docs)
    and are unrelated to QC selection: the caller passes whatever license
    name(s)/count(s) their site has configured for the target QC, verbatim,
    as Slurm's own `name[:count][,name[:count]...]` syntax.

    Returns:
        List of `srun` options requesting *licenses*, or an empty list if none
        was given.
    """
    return [f"--licenses={licenses}"] if licenses else []


def _resolve_partition(partition: str | None) -> str:
    """Resolve the Slurm partition the `srun` job is submitted to.

    The partition holding the QC nodes carries whatever name its administrator
    gave it, so an explicit *partition* takes precedence over the
    `IQM_SLURM_PARTITION` environment variable, which in turn takes precedence
    over the `quantum` name the administrator guide provisions. This mirrors how
    the QDMI-on-IQM SPANK plugin's `IQM_BASE_URL`/`IQM_QC_ID`/`IQM_QC_ALIAS`
    variables let a site set a default once for every user. Slurm's own
    `SLURM_PARTITION` cannot serve that role here, because the resolved name is
    always passed as an explicit `--partition`, which overrides it.

    Returns:
        The partition name to pass to `srun`.
    """
    return partition or os.getenv("IQM_SLURM_PARTITION") or _DEFAULT_PARTITION


def _run_srun(
    command: list[str],
    job_dir: Path,
    *,
    timeout: float | None = None,
) -> subprocess.CompletedProcess[bytes]:
    """Run *command* via `srun` and return the completed process.

    On failure, *job_dir* (containing the serialized inputs) is intentionally
    left on disk rather than cleaned up, so its path is included in the raised
    error to make it discoverable for debugging.

    Returns:
        The completed process, with captured stdout/stderr.

    Raises:
        RuntimeError: If the Slurm job times out or returns a non-zero exit
            code.
    """
    try:
        process = subprocess.run(command, capture_output=True, check=False, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        msg = f"Slurm job timed out after {timeout}s (job inputs kept at {job_dir} for debugging)"
        raise RuntimeError(msg) from e
    if process.returncode != 0:
        stderr = process.stderr.decode().strip()
        msg = f"Error while submitting job to Slurm: {stderr} (job inputs kept at {job_dir} for debugging)"
        raise RuntimeError(msg)
    return process


def _load_json_result(process: subprocess.CompletedProcess[bytes]) -> object:
    """Parse the job's stdout as JSON.

    Returns:
        The parsed result.

    Raises:
        RuntimeError: If the job produced no valid JSON output.
    """
    if not process.stdout.strip():
        msg = "No output from the job."
        raise RuntimeError(msg)
    try:
        return json.loads(process.stdout)
    except ValueError as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e


def _encode_vqe_result(result: VQEResult) -> str:
    """Serialize the VQE fields returned by the offloader as JSON.

    Returns:
        The serialized result.
    """
    optimizer = cast("OptimizerResult", result.optimizer_result)
    payload = {
        "optimizer_result": {name: getattr(optimizer, name) for name in ("x", "fun", "jac", "nfev", "njev", "nit")},
        "optimizer_time": result.optimizer_time,
    }
    return json.dumps(payload, default=lambda value: value.tolist(), allow_nan=False)


def _finite_float(value: object) -> float:
    """Validate a real number from JSON.

    Returns:
        The finite value as a float.

    Raises:
        ValueError: If the value is not a finite JSON number.
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        msg = "Expected a finite JSON number."
        raise ValueError(msg)
    return float(value)


def _parameter_vector(value: object, size: int) -> NDArray[np.float64]:
    """Validate a flat JSON array with one finite value per ansatz parameter.

    Returns:
        The parameter vector as a NumPy array.

    Raises:
        ValueError: If the vector has an incompatible shape or value.
    """
    if not isinstance(value, list) or len(value) != size:
        msg = f"Expected {size} parameter values."
        raise ValueError(msg)
    return np.asarray([_finite_float(item) for item in value], dtype=float)


def _decode_vqe_result(payload: object, ansatz: QuantumCircuit) -> VQEResult:
    """Reconstruct a VQE result from its JSON payload and original ansatz.

    Returns:
        The reconstructed result.

    Raises:
        RuntimeError: If the payload does not contain the expected fields.
    """
    try:
        data = cast("Mapping[str, object]", payload)
        optimizer_data = cast("Mapping[str, object]", data["optimizer_result"])
        point = _parameter_vector(optimizer_data["x"], ansatz.num_parameters)
        fun = _finite_float(optimizer_data["fun"])
        jac_data = optimizer_data["jac"]
        jac = None if jac_data is None else _parameter_vector(jac_data, ansatz.num_parameters)
        nfev = _nonnegative_int(optimizer_data["nfev"])
        njev_data = optimizer_data["njev"]
        njev = None if njev_data is None else _nonnegative_int(njev_data)
        nit_data = optimizer_data["nit"]
        nit = None if nit_data is None else _nonnegative_int(nit_data)
        optimizer_time = _finite_float(data["optimizer_time"])
        optimal_parameters = dict(zip(ansatz.parameters, point, strict=True))
    except (KeyError, TypeError, ValueError, OverflowError) as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e

    if optimizer_time < 0:
        msg = "Error parsing the output: optimizer time must be nonnegative."
        raise RuntimeError(msg)

    optimizer_result = OptimizerResult()
    optimizer_result.x = point
    optimizer_result.fun = fun
    optimizer_result.jac = jac
    optimizer_result.nfev = nfev
    optimizer_result.njev = njev
    optimizer_result.nit = nit

    result = VQEResult()
    result.optimal_circuit = ansatz.copy()
    result.eigenvalue = fun
    result.cost_function_evals = nfev
    result.optimal_point = point
    result.optimal_parameters = optimal_parameters
    result.optimal_value = fun
    result.optimizer_time = optimizer_time
    result.optimizer_result = optimizer_result
    return result


def sample(
    qc: QuantumCircuit,
    shots: int = 1024,
    *,
    local: bool = False,
    simulator: bool = False,
    timeout: float | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
    licenses: str | None = None,
    partition: str | None = None,
    nodes: int = _DEFAULT_NODES,
) -> dict[str, int]:
    """Sample from a quantum circuit.

    When `local=False` (default), serializes the given circuit to QPY format
    and submits it to the Slurm workload manager using the `srun` command.
    After completion, the counts are parsed and returned as a dictionary.

    When `local=True`, runs the circuit in this process on the selected backend.

    Args:
        qc: The quantum circuit to run.
        shots: The number of shots to run. Default is 1024.
        local: If True, run the job in this process on the selected backend.
            If False (default), offload to Slurm.
        simulator: If True, run the job on the simulator instead of the quantum computer.
        timeout: How long to wait for the Slurm job to complete, in seconds,
            before giving up. Only used when `local=False`.
        qc_id: If given, passed as `--iqm-qc-id` to `srun`, which the QDMI-on-IQM
            SPANK plugin resolves into the `IQM_QC_ID` job environment variable.
            Only used when `local=False`.
        qc_alias: If given, passed as `--iqm-qc-alias` to `srun`, which the
            QDMI-on-IQM SPANK plugin resolves into the `IQM_QC_ALIAS` job
            environment variable. Only used when `local=False`.
        licenses: If given, passed as `--licenses` to `srun`, requesting the
            named Slurm license(s) (Slurm's own `name[:count][,name[:count]
            ...]` syntax) that a site administrator may have configured to
            cap concurrent jobs against a QC -- e.g. required by the SPANK
            plugin's `iqm_require_license` option. Only used when
            `local=False`.
        partition: The Slurm partition to submit to, passed as `--partition`
            to `srun`. Defaults to the `IQM_SLURM_PARTITION` environment
            variable, and to `quantum` when that is unset. Only used when
            `local=False`.
        nodes: The number of nodes to allocate, passed as `--nodes` to `srun`.
            The worker always runs as a single task (`--ntasks=1`), so this
            only sizes the allocation for sites whose partition demands more
            than one node. Default is 1. Only used when `local=False`.

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
        sampler = build_sampler(simulator=simulator)
        qc_for_execution = transpile(qc, sampler.backend, optimization_level=TRANSPILE_OPTIMIZATION_LEVEL)
        job = sampler.run([(qc_for_execution,)], shots=shots)
        return extract_counts(job.result())

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
        f"--nodes={nodes}",
        "--ntasks=1",
        f"--partition={_resolve_partition(partition)}",
        *_spank_qc_selection_args(qc_id, qc_alias),
        *_licenses_arg(licenses),
        "iqm-sampler",
        str(qc_path.absolute()),
        "--shots",
        str(shots),
    ]
    if simulator:
        command.append("--simulator")

    process = _run_srun(command, job_dir, timeout=timeout)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    counts = _load_json_result(process)
    if not isinstance(counts, dict) or any(not bits or set(bits) - {"0", "1"} for bits in counts):
        msg = "Error parsing the output: expected a JSON object mapping binary strings to counts."
        raise RuntimeError(msg)
    try:
        return {bits: _nonnegative_int(count) for bits, count in counts.items()}
    except ValueError as e:
        msg = f"Error parsing the output: {e}"
        raise RuntimeError(msg) from e


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
    licenses: str | None = None,
    partition: str | None = None,
    nodes: int = _DEFAULT_NODES,
) -> VQEResult:
    """Estimate the optimal parameters for a given ansatz circuit and operator.

    When `local=False` (default), serializes the given ansatz and operator to
    QPY/pickle format and submits them to the Slurm workload manager using the
    `srun` command. After completion, the VQE result is parsed and returned.

    When `local=True`, runs the VQE algorithm locally using either the MQT Core
    DDSIM simulator backend or the packaged IQM backend.

    Both paths run `VQE` with `L_BFGS_B` and no auxiliary operators, returning
    the optimal circuit, parameter values, and optimizer state.

    Args:
        ansatz: The ansatz circuit to run.
        operator: The operator to run.
        maxiter: The maximum number of iterations for the optimization.
            Default is 80.
        local: If True, run the job in this process on the selected backend.
            If False (default), offload to Slurm.
        simulator: If True, run the job on the simulator instead of the quantum computer.
        timeout: How long to wait for the Slurm job to complete, in seconds,
            before giving up. Only used when `local=False`.
        qc_id: If given, passed as `--iqm-qc-id` to `srun`, which the QDMI-on-IQM
            SPANK plugin resolves into the `IQM_QC_ID` job environment variable.
            Only used when `local=False`.
        qc_alias: If given, passed as `--iqm-qc-alias` to `srun`, which the
            QDMI-on-IQM SPANK plugin resolves into the `IQM_QC_ALIAS` job
            environment variable. Only used when `local=False`.
        licenses: If given, passed as `--licenses` to `srun`, requesting the
            named Slurm license(s) (Slurm's own `name[:count][,name[:count]
            ...]` syntax) that a site administrator may have configured to
            cap concurrent jobs against a QC -- e.g. required by the SPANK
            plugin's `iqm_require_license` option. Only used when
            `local=False`.
        partition: The Slurm partition to submit to, passed as `--partition`
            to `srun`. Defaults to the `IQM_SLURM_PARTITION` environment
            variable, and to `quantum` when that is unset. Only used when
            `local=False`.
        nodes: The number of nodes to allocate, passed as `--nodes` to `srun`.
            The worker always runs as a single task (`--ntasks=1`), so this
            only sizes the allocation for sites whose partition demands more
            than one node. Default is 1. Only used when `local=False`.

    Returns:
        The VQE result, including the optimal parameters and eigenvalue.

    Raises:
        ImportError: If Qiskit or the QDMI backend plugins are not installed.
        RuntimeError: If there is an error while submitting the job to Slurm or parsing the output.
    """  # ruff:ignore[docstring-extraneous-exception]
    if _IMPORT_ERROR is not None:
        msg = (
            "Failed to import Qiskit and QDMI backend plugins. "
            "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
        )
        raise ImportError(msg) from _IMPORT_ERROR
    if local:
        estimator = build_estimator(simulator=simulator)
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
        f"--nodes={nodes}",
        "--ntasks=1",
        f"--partition={_resolve_partition(partition)}",
        *_spank_qc_selection_args(qc_id, qc_alias),
        *_licenses_arg(licenses),
        "iqm-estimator",
        str(qc_path.absolute()),
        str(operator_path.absolute()),
        "--maxiter",
        str(maxiter),
    ]
    if simulator:
        command.append("--simulator")

    process = _run_srun(command, job_dir, timeout=timeout)

    # Cleanup artifacts after successful completion.
    qc_path.unlink(missing_ok=True)
    operator_path.unlink(missing_ok=True)
    with contextlib.suppress(OSError):
        job_dir.rmdir()

    return _decode_vqe_result(_load_json_result(process), ansatz)
