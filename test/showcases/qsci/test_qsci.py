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

"""Showcase tests for QSCI using the QDMI Qiskit integration."""

from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING

import pytest

THIS_DIR = Path(__file__).resolve().parent
SHOWCASES_DIR = THIS_DIR.parent
for path in (THIS_DIR, SHOWCASES_DIR):
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

qiskit_algorithms = pytest.importorskip(
    "qiskit_algorithms",
    reason=QSCI_PY314_REASON,
)
qiskit_algorithms_optimizers = pytest.importorskip("qiskit_algorithms.optimizers", reason=QSCI_PY314_REASON)
qiskit_quantum_info = pytest.importorskip("qiskit.quantum_info", reason=QSCI_PY314_REASON)
pytest.importorskip(
    "qiskit_nature",
    reason=QSCI_PY314_REASON,
)
qiskit_nature_algorithms = pytest.importorskip("qiskit_nature.second_q.algorithms", reason=QSCI_PY314_REASON)
qiskit_nature_circuit_library = pytest.importorskip("qiskit_nature.second_q.circuit.library", reason=QSCI_PY314_REASON)
qiskit_nature_drivers = pytest.importorskip("qiskit_nature.second_q.drivers", reason=QSCI_PY314_REASON)
qiskit_nature_initial_points = pytest.importorskip(
    "qiskit_nature.second_q.algorithms.initial_points", reason=QSCI_PY314_REASON
)
qiskit_nature_mappers = pytest.importorskip("qiskit_nature.second_q.mappers", reason=QSCI_PY314_REASON)
pytest.importorskip(
    "pyscf",
    reason=QSCI_WINDOWS_REASON,
)

postprocess = importlib.import_module("postprocess")
support = importlib.import_module("support")

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from qiskit.circuit.quantumcircuit import QuantumCircuit
    from qiskit.quantum_info import SparsePauliOp
    from qiskit_nature.second_q.circuit.library import UCCSD
    from qiskit_nature.second_q.mappers import JordanWignerMapper
    from qiskit_nature.second_q.problems import ElectronicStructureProblem

QSCI_MAXITER = int(os.getenv("IQM_QSCI_MAXITER", "30"))
QSCI_SHOTS = int(os.getenv("IQM_QSCI_SHOTS", "2048"))
QSCI_CUTOFF = int(os.getenv("IQM_QSCI_CUTOFF", "10"))


def prepare_ansatz(
    num_spatial_orbitals: int,
    num_particles: tuple[int, int],
) -> tuple[JordanWignerMapper, UCCSD]:
    """Construct the mapped ansatz used in the QSCI showcase.

    Args:
        num_spatial_orbitals: The number of spatial orbitals in the problem.
        num_particles: The number of alpha and beta electrons.

    Returns:
        The Jordan-Wigner mapper together with the configured UCCSD ansatz.
    """
    mapper = qiskit_nature_mappers.JordanWignerMapper()
    initial_state = qiskit_nature_circuit_library.HartreeFock(
        num_spatial_orbitals,
        num_particles,
        mapper,
    )
    ansatz = qiskit_nature_circuit_library.UCCSD(
        num_spatial_orbitals,
        num_particles,
        mapper,
        initial_state=initial_state,
    )
    return mapper, ansatz


def train_ansatz(
    backend: QDMIBackend,
    ansatz: UCCSD,
    observable: SparsePauliOp,
    problem: ElectronicStructureProblem,
) -> QuantumCircuit:
    """Optimize the ansatz parameters using the backend-bound estimator.

    Args:
        backend: The selected QDMI backend used for estimator evaluations.
        ansatz: The parameterized ansatz circuit to optimize.
        observable: The mapped observable whose expectation value is minimized.
        problem: The electronic-structure problem used to derive a deterministic MP2 initial point.

    Returns:
        The ansatz circuit with the optimized parameters assigned.

    Raises:
        ValueError: If VQE does not return optimal parameters or parameter assignment fails.
    """
    vqe_ansatz = ansatz
    if support.showcase_backend_kind() is support.ShowcaseBackend.DDSIM:
        vqe_ansatz = support.transpile_for_backend(backend, ansatz)

    estimator = support.make_vqe_estimator(backend, shots=QSCI_SHOTS)
    mp2_initial_point = qiskit_nature_initial_points.MP2InitialPoint()
    mp2_initial_point.compute(ansatz=ansatz, problem=problem)
    vqe = qiskit_algorithms.VQE(
        estimator,
        vqe_ansatz,
        qiskit_algorithms_optimizers.L_BFGS_B(maxiter=QSCI_MAXITER),
        initial_point=mp2_initial_point.to_numpy_array(),
    )
    result = vqe.compute_minimum_eigenvalue(operator=observable)
    if result.optimal_parameters is None:
        msg = "VQE must return optimal parameters"
        raise ValueError(msg)

    trained_ansatz = vqe_ansatz.assign_parameters(result.optimal_parameters)
    if trained_ansatz is None:
        msg = "assign_parameters should return a bound circuit"
        raise ValueError(msg)

    return trained_ansatz


def energy_tolerance() -> float:
    """Return the accepted energy deviation for the selected target.

    Returns:
        The allowed deviation between the exact and QSCI energies.
    """
    return float(os.getenv("IQM_QSCI_ENERGY_TOLERANCE", "0.35"))


@pytest.mark.qsci
def test_h2_qsci_showcase(backend: QDMIBackend) -> None:
    """Run the H2 QSCI showcase end to end on the selected backend."""
    atom = "H 0 0 0; H 0 0 1;"
    basis = "sto-3g"

    driver = qiskit_nature_drivers.PySCFDriver(atom=atom, basis=basis)
    problem = driver.run()
    nuclear_repulsion_energy = problem.nuclear_repulsion_energy

    assert problem.num_spatial_orbitals is not None, "num_spatial_orbitals is required"
    assert problem.num_particles is not None, "num_particles is required"
    assert nuclear_repulsion_energy is not None, "nuclear_repulsion_energy is required"

    mapper, ansatz = prepare_ansatz(problem.num_spatial_orbitals, problem.num_particles)
    observable = mapper.map(problem.second_q_ops()[0])
    assert isinstance(observable, qiskit_quantum_info.SparsePauliOp), "mapped observable must be a SparsePauliOp"
    support.skip_if_backend_too_small(backend, required_qubits=ansatz.num_qubits)

    trained_ansatz = train_ansatz(backend, ansatz, observable, problem)
    counts = support.sample_counts(backend, trained_ansatz, shots=QSCI_SHOTS)

    eigval = postprocess.postprocess_counts(atom, basis, counts, cutoff=QSCI_CUTOFF)
    qsci_energy = eigval + nuclear_repulsion_energy

    exact_solver = qiskit_nature_algorithms.GroundStateEigensolver(
        mapper,
        qiskit_algorithms.NumPyMinimumEigensolver(),
    )
    exact_result = exact_solver.solve(problem)
    assert exact_result.total_energies is not None, "exact solver must return total energies"
    exact_energy = float(exact_result.total_energies[0])
    tolerance = energy_tolerance()
    energy_difference = abs(exact_energy - qsci_energy)

    assert energy_difference < tolerance, (
        f"QSCI energy deviated from the exact reference by {energy_difference:.6f}, "
        f"expected less than {tolerance:.6f} (exact={exact_energy:.6f}, qsci={qsci_energy:.6f})."
    )
