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

"""Shared helpers for use-case workflow tests."""

from __future__ import annotations

import os
from typing import TYPE_CHECKING

import pytest
from qiskit import transpile

if TYPE_CHECKING:
    from qiskit.circuit import QuantumCircuit

    from iqm.qdmi.qiskit import IQMBackend


def require_iqm_access() -> None:
    """Skip use-case workflow tests when IQM credentials are unavailable."""
    if any(os.getenv(name) for name in ("RESONANCE_API_KEY", "IQM_TOKEN", "IQM_TOKENS_FILE")):
        return

    pytest.skip(
        "Set RESONANCE_API_KEY, IQM_TOKEN, or IQM_TOKENS_FILE to run the QDMI use-case workflows.",
        allow_module_level=True,
    )


def selected_target_is_mock() -> bool:
    """Return whether the selected IQM target looks like a mock backend.

    Returns:
        True when the selected target identifier suggests a mock backend.
    """
    target_hint = " ".join(filter(None, (os.getenv("IQM_QC_ALIAS"), os.getenv("IQM_QC_ID")))).lower()
    return "mock" in target_hint


def transpile_for_backend(backend: IQMBackend, circuit: QuantumCircuit) -> QuantumCircuit:
    """Transpile a circuit for the configured IQM backend.

    Args:
        backend: The target IQM backend for transpilation.
        circuit: The quantum circuit to transpile.

    Returns:
        The transpiled circuit ready for execution on the backend.
    """
    return transpile(circuit, backend=backend, optimization_level=3)


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


def sample_counts(backend: IQMBackend, circuit: QuantumCircuit, *, shots: int) -> dict[str, int]:
    """Execute a circuit through the backend-bound sampler and return counts.

    Args:
        backend: The IQM backend providing the bound sampler.
        circuit: The quantum circuit to execute.
        shots: The number of shots to run.

    Returns:
        The measured bitstring counts from the sampler result.
    """
    measured_circuit = ensure_measurements(circuit)
    transpiled_circuit = transpile_for_backend(backend, measured_circuit)
    job = backend.sampler(default_shots=shots).run([(transpiled_circuit,)], shots=shots)
    return job.result()[0].data["meas"].get_counts()


def skip_if_backend_too_small(backend: IQMBackend, *, required_qubits: int) -> None:
    """Skip when the selected target does not expose enough qubits.

    Args:
        backend: The IQM backend selected for the workflow.
        required_qubits: The minimum number of qubits needed.
    """
    if backend.num_qubits >= required_qubits:
        return

    pytest.skip(
        f"Selected target exposes {backend.num_qubits} qubits, but this workflow needs {required_qubits}.",
        allow_module_level=False,
    )
