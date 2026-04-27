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

# pyright: reportArgumentType=false, reportUnknownArgumentType=false, reportUnknownMemberType=false, reportUnknownVariableType=false, reportMissingImports=false, reportMissingModuleSource=false, reportMissingTypeStubs=false

"""Tests for the Qiskit-facing IQM backend wrapper."""

from __future__ import annotations

import os

import pytest
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile
from qiskit.quantum_info import SparsePauliOp

from iqm.qdmi.qiskit import IQMBackend


def _skip_without_iqm_access() -> None:
    """Skip live tests when IQM credentials are unavailable."""
    if not os.getenv("RESONANCE_API_KEY") and not os.getenv("IQM_TOKEN"):
        pytest.skip(
            "Either RESONANCE_API_KEY or IQM_TOKEN environment variable must be set to run live IQM backend tests."
        )


@pytest.fixture
def backend() -> IQMBackend:
    """Returns the IQM backend."""
    _skip_without_iqm_access()
    return IQMBackend()


@pytest.fixture
def circuit() -> QuantumCircuit:
    """Returns a simple Bell state circuit."""
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    return circuit


def test_iqm_backend(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """Test the execution of a simple Bell state circuit."""
    circuit.measure_all()
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.run(transpiled_circuit, shots=8)
    counts = job.result().get_counts()
    assert sum(counts.values()) == 8


def test_iqm_backend_sampler(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """The bound sampler should execute a simple circuit on the live IQM backend."""
    circuit.measure_all()
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.sampler().run([(transpiled_circuit,)], shots=8)
    counts = job.result()[0].data["meas"].get_counts()
    assert sum(counts.values()) == 8


def test_iqm_backend_estimator(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """The bound estimator should execute a simple observable on the live IQM backend."""
    observable = SparsePauliOp("Z" * backend.num_qubits)
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.estimator(options={"default_shots": 32}).run([(transpiled_circuit, observable)])
    result = job.result()[0]
    expectation_value = float(result.data["evs"][()])
    standard_deviation = float(result.data["stds"][()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    assert result.metadata["shots"] == 32
