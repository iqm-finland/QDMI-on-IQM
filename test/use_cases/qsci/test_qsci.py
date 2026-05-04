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

"""Use-case workflow tests for QSCI using the QDMI Qiskit integration."""

from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING, cast

import pytest

THIS_DIR = Path(__file__).resolve().parent
USE_CASES_DIR = THIS_DIR.parent
for path in (THIS_DIR, USE_CASES_DIR):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

QSCI_PY314_REASON = (
    "QSCI requires the pinned Qiskit 1.x, Qiskit Algorithms, and Qiskit Nature stack. "
    "Install the showcase dependencies on Python < 3.14 to run it."
)
QSCI_WINDOWS_REASON = (
    "QSCI requires PySCF, which is not supported on Windows. "
    "Install the showcase dependencies on a non-Windows platform to run it."
)

pytest.importorskip(
    "qiskit_algorithms",
    reason=QSCI_PY314_REASON,
)
qiskit_algorithms_optimizers = pytest.importorskip("qiskit_algorithms.optimizers", reason=QSCI_PY314_REASON)
pytest.importorskip(
    "qiskit_nature",
    reason=QSCI_PY314_REASON,
)
qiskit_nature_algorithms = pytest.importorskip("qiskit_nature.second_q.algorithms", reason=QSCI_PY314_REASON)
qiskit_nature_circuit_library = pytest.importorskip("qiskit_nature.second_q.circuit.library", reason=QSCI_PY314_REASON)
qiskit_nature_drivers = pytest.importorskip("qiskit_nature.second_q.drivers", reason=QSCI_PY314_REASON)
qiskit_nature_mappers = pytest.importorskip("qiskit_nature.second_q.mappers", reason=QSCI_PY314_REASON)
pytest.importorskip(
    "pyscf",
    reason=QSCI_WINDOWS_REASON,
)

qiskit_algorithms = importlib.import_module("qiskit_algorithms")
postprocess_module = importlib.import_module("postprocess")
support_module = importlib.import_module("support")

VQE = qiskit_algorithms.VQE
NumPyMinimumEigensolver = qiskit_algorithms.NumPyMinimumEigensolver
L_BFGS_B = qiskit_algorithms_optimizers.L_BFGS_B
GroundStateEigensolver = qiskit_nature_algorithms.GroundStateEigensolver
UCCSD = qiskit_nature_circuit_library.UCCSD
HartreeFock = qiskit_nature_circuit_library.HartreeFock
PySCFDriver = qiskit_nature_drivers.PySCFDriver
JordanWignerMapper = qiskit_nature_mappers.JordanWignerMapper
postprocess_counts = postprocess_module.postprocess_counts
sample_counts = support_module.sample_counts
selected_target_is_mock = support_module.selected_target_is_mock
skip_if_backend_too_small = support_module.skip_if_backend_too_small

if TYPE_CHECKING:
    from collections.abc import Mapping

    from qiskit.circuit.parameter import Parameter
    from qiskit.circuit.parameterexpression import ParameterExpression
    from qiskit.circuit.quantumcircuit import QuantumCircuit
    from qiskit.primitives.base.base_estimator import BaseEstimator
    from qiskit.quantum_info import SparsePauliOp
    from qiskit_nature.second_q.operators import FermionicOp
    from qiskit_nature.second_q.problems import ElectronicStructureResult

    from iqm.qdmi.qiskit import IQMBackend

QSCI_MAXITER = int(os.getenv("IQM_QSCI_MAXITER", "30"))
QSCI_SHOTS = int(os.getenv("IQM_QSCI_SHOTS", "2048"))
QSCI_CUTOFF = int(os.getenv("IQM_QSCI_CUTOFF", "10"))


def prepare_ansatz(
    num_spatial_orbitals: int,
    num_particles: tuple[int, int],
) -> tuple[JordanWignerMapper, UCCSD]:
    """Construct the mapped ansatz used in the QSCI workflow.

    Returns:
        The Jordan-Wigner mapper together with the configured UCCSD ansatz.
    """
    mapper = JordanWignerMapper()
    initial_state = HartreeFock(
        num_spatial_orbitals,
        num_particles,
        mapper,
    )
    ansatz = UCCSD(
        num_spatial_orbitals,
        num_particles,
        mapper,
        initial_state=initial_state,
    )
    return mapper, ansatz


def train_ansatz(
    backend: IQMBackend,
    ansatz: UCCSD,
    observable: SparsePauliOp,
) -> QuantumCircuit:
    """Optimize the ansatz parameters using the backend-bound estimator.

    Returns:
        The ansatz circuit with the optimized parameters assigned.
    """
    estimator = cast("BaseEstimator", backend.estimator(options={"default_shots": QSCI_SHOTS}))
    vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=QSCI_MAXITER))
    result = vqe.compute_minimum_eigenvalue(operator=observable)
    assert result.optimal_parameters is not None, "VQE must return optimal parameters"
    trained_ansatz = ansatz.assign_parameters(
        cast("Mapping[Parameter, ParameterExpression | float]", result.optimal_parameters)
    )
    assert trained_ansatz is not None, "assign_parameters should return a bound circuit"
    return trained_ansatz


def energy_tolerance() -> float:
    """Return the accepted energy deviation for the selected target.

    Returns:
        The allowed deviation between the exact and QSCI energies.
    """
    target_is_mock = selected_target_is_mock()
    default_tolerance = "0.15" if target_is_mock else "0.75"
    return float(os.getenv("IQM_QSCI_ENERGY_TOLERANCE", default_tolerance))


@pytest.mark.qsci
def test_h2_qsci_workflow(backend: IQMBackend) -> None:
    """Run the H2 QSCI showcase workflow end to end on the selected backend."""
    atom = "H 0 0 0; H 0 0 1;"
    basis = "sto-3g"

    driver = PySCFDriver(atom=atom, basis=basis)
    problem = driver.run()
    nuclear_repulsion_energy = problem.nuclear_repulsion_energy

    assert problem.num_spatial_orbitals is not None, "num_spatial_orbitals is required"
    assert problem.num_particles is not None, "num_particles is required"
    assert nuclear_repulsion_energy is not None, "nuclear_repulsion_energy is required"

    mapper, ansatz = prepare_ansatz(problem.num_spatial_orbitals, problem.num_particles)
    observable = cast("SparsePauliOp", mapper.map(cast("FermionicOp", problem.second_q_ops()[0])))
    skip_if_backend_too_small(backend, required_qubits=ansatz.num_qubits)

    trained_ansatz = train_ansatz(backend, ansatz, observable)
    counts = sample_counts(backend, trained_ansatz, shots=QSCI_SHOTS)

    eigval = postprocess_counts(atom, basis, counts, cutoff=QSCI_CUTOFF)
    qsci_energy = eigval + nuclear_repulsion_energy

    exact_solver = GroundStateEigensolver(mapper, NumPyMinimumEigensolver())
    exact_result = cast("ElectronicStructureResult", exact_solver.solve(problem))
    assert exact_result.total_energies is not None, "exact solver must return total energies"
    exact_energy = float(exact_result.total_energies[0])

    assert abs(exact_energy - qsci_energy) < energy_tolerance()
