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

"""Shared helpers for showcase tests."""

from __future__ import annotations

import importlib
import os
from enum import Enum
from typing import TYPE_CHECKING

import pytest
from mqt.core.plugins.qiskit.estimator import QDMIEstimator
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit import transpile

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from qiskit.circuit import QuantumCircuit
    from qiskit.primitives import BackendEstimator

SHOWCASE_BACKEND_ENV = "IQM_SHOWCASE_BACKEND"


class ShowcaseBackend(str, Enum):
    """Supported showcase backend kinds."""

    IQM = "iqm"
    DDSIM = "ddsim"


def showcase_backend_kind() -> ShowcaseBackend:
    """Return the configured showcase backend kind.

    Returns:
        The normalized showcase backend selection.

    Raises:
        ValueError: If the showcase backend selection is unsupported.
    """
    backend_kind = os.getenv(SHOWCASE_BACKEND_ENV, "iqm").strip().lower()
    try:
        return ShowcaseBackend(backend_kind)
    except ValueError as exc:
        expected_values = ", ".join(backend.value for backend in ShowcaseBackend)
        msg = f"{SHOWCASE_BACKEND_ENV} must be one of {expected_values}, got {backend_kind!r}."
        raise ValueError(msg) from exc


def require_showcase_backend_access() -> None:
    """Skip showcase workflows when the selected backend is not configured."""
    if showcase_backend_kind() is not ShowcaseBackend.IQM:
        return

    if any(os.getenv(name) for name in ("RESONANCE_API_KEY", "IQM_TOKEN", "IQM_TOKENS_FILE")):
        return

    pytest.skip(
        "Set RESONANCE_API_KEY, IQM_TOKEN, or IQM_TOKENS_FILE to run the QDMI showcases.",
        allow_module_level=True,
    )


def transpile_for_backend(backend: QDMIBackend, circuit: QuantumCircuit) -> QuantumCircuit:
    """Transpile a circuit for the configured showcase backend.

    Args:
        backend: The target QDMI backend for transpilation.
        circuit: The quantum circuit to transpile.

    Returns:
        The transpiled circuit ready for execution on the backend.
    """
    if showcase_backend_kind() is ShowcaseBackend.DDSIM:
        # The DDSIM backend's Target metadata does not round-trip through
        # qiskit.transpile(backend=...), so decompose against the exposed basis
        # gates without asking Qiskit to build backend properties from Target.
        return transpile(circuit, basis_gates=sorted(backend.operation_names), optimization_level=1)

    return transpile(circuit, backend=backend, optimization_level=3)


def make_sampler(backend: QDMIBackend, *, shots: int) -> QDMISampler:
    """Return a sampler primitive for the selected QDMI backend.

    Args:
        backend: The QDMI backend that should execute the sampling jobs.
        shots: The default number of shots for the primitive.

    Returns:
        A :class:`~mqt.core.plugins.qiskit.sampler.QDMISampler` bound to the selected backend.
    """
    return QDMISampler(backend, default_shots=shots)


def make_estimator(backend: QDMIBackend, *, shots: int) -> QDMIEstimator:
    """Return an estimator primitive for the selected QDMI backend.

    Args:
        backend: The QDMI backend that should execute the estimator jobs.
        shots: The default shot budget for expectation-value estimation.

    Returns:
        A :class:`~mqt.core.plugins.qiskit.estimator.QDMIEstimator` bound to the selected backend.
    """
    return QDMIEstimator(backend, options={"default_shots": shots})


def make_vqe_estimator(backend: QDMIBackend, *, shots: int) -> BackendEstimator:
    """Return a :class:`~qiskit_algorithms.minimum_eigensolvers.VQE`-compatible estimator.

    Args:
        backend: The QDMI backend that should execute the VQE jobs.
        shots: The backend-estimator shot budget.

    Returns:
        A :class:`~qiskit.primitives.BackendEstimator` instance using the Estimator V1
        compatibility layer.
    """
    backend_estimator = importlib.import_module("qiskit.primitives").BackendEstimator

    skip_transpilation = showcase_backend_kind() is ShowcaseBackend.DDSIM
    # qiskit_algorithms.VQE still expects the Estimator V1 interface, so use
    # BackendEstimator here even though the native QDMI estimator is V2.
    return backend_estimator(
        backend=backend,
        options={"shots": shots},
        skip_transpilation=skip_transpilation,
    )


def ensure_measurements(circuit: QuantumCircuit) -> QuantumCircuit:
    """Return a copy of the circuit with measurements attached.

    Args:
        circuit: The quantum circuit to copy and augment with measurements.

    Returns:
        A circuit that includes measurements on all qubits.
    """
    measured_circuit = circuit.copy()
    if any(instruction.operation.name == "measure" for instruction in measured_circuit.data):
        return measured_circuit

    measured_circuit.measure_all()
    return measured_circuit


def sample_counts(backend: QDMIBackend, circuit: QuantumCircuit, *, shots: int) -> dict[str, int]:
    """Execute a circuit through the backend-bound sampler and return counts.

    Args:
        backend: The QDMI backend providing the bound sampler.
        circuit: The quantum circuit to execute.
        shots: The number of shots to run.

    Returns:
        The measured bitstring counts from the sampler result.

    Raises:
        ValueError: If the sampler result does not expose classical measurement data.
    """
    measured_circuit = ensure_measurements(circuit)
    transpiled_circuit = transpile_for_backend(backend, measured_circuit)
    job = make_sampler(backend, shots=shots).run([(transpiled_circuit,)], shots=shots)
    data = job.result()[0].data
    if "meas" in data:
        return data["meas"].get_counts()

    register_name = next(iter(data.keys()), None)
    if register_name is None:
        msg = "Sampler result did not contain any classical measurement data."
        raise ValueError(msg)

    return data[register_name].get_counts()


def skip_if_backend_too_small(backend: QDMIBackend, *, required_qubits: int) -> None:
    """Skip when the selected target does not expose enough qubits.

    Args:
        backend: The QDMI backend selected for the showcase.
        required_qubits: The minimum number of qubits needed.
    """
    if backend.num_qubits >= required_qubits:
        return

    pytest.skip(
        f"Selected target exposes {backend.num_qubits} qubits, but this showcase needs {required_qubits}.",
        allow_module_level=False,
    )
