#!/usr/bin/env -S uv run --script --quiet
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

# /// script
# requires-python = ">=3.10,<3.14"
# dependencies = [
#   "iqm-qdmi[qiskit]",
#   "qiskit-nature[pyscf]>=0.7.2",
# ]
#
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# [tool.ty.analysis]
# allowed-unresolved-imports = ["qiskit_nature.**", "pyscf.**"]
# ///

"""Run QSCI on the H2 molecule using the QDMI-on-IQM stack."""

from __future__ import annotations

import argparse
import logging
import sys
from functools import reduce
from typing import TYPE_CHECKING, cast

import numpy as np
import scipy.linalg as sla
from mqt.core.plugins.qiskit.estimator import QDMIEstimator
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from pyscf import ao2mo, gto, scf
from qiskit.compiler import transpile
from qiskit_algorithms.minimum_eigensolvers.vqe import VQE
from qiskit_algorithms.optimizers.l_bfgs_b import L_BFGS_B
from qiskit_nature.second_q.circuit.library import UCCSD, HartreeFock
from qiskit_nature.second_q.drivers import PySCFDriver
from qiskit_nature.second_q.mappers import JordanWignerMapper
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigs

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from qiskit.quantum_info import SparsePauliOp

log = logging.getLogger(__name__)

ATOM = "H 0 0 0; H 0 0 1;"
BASIS = "sto-3g"
EXPECTED_ENERGY = -1.101150


def main() -> None:
    """Runs QSCI on the H2 molecule."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S",
    )

    if sys.platform == "win32":
        sys.exit("QSCI requires PySCF, which is not supported on Windows.")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--shots", type=int, default=8192)
    parser.add_argument("--maxiter", type=int, default=30)
    parser.add_argument("--cutoff", type=int, default=10)
    args = parser.parse_args()
    log.info(
        "Starting QSCI/H2 example (backend=%s, shots=%d, maxiter=%d, cutoff=%d)",
        args.backend,
        args.shots,
        args.maxiter,
        args.cutoff,
    )

    log.info("Initialising '%s' backend...", args.backend)
    backend = IQMBackend() if args.backend == "iqm" else QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
    log.info("Backend ready: '%s' | %d qubits", backend.name, backend.num_qubits)

    log.info("Setting up H2 problem (atom='%s', basis='%s')...", ATOM.strip(), BASIS)
    driver = PySCFDriver(atom=ATOM, basis=BASIS)
    problem = driver.run()
    mapper = JordanWignerMapper()
    initial_state = HartreeFock(problem.num_spatial_orbitals, problem.num_particles, mapper)
    num_spatial_orbitals = problem.num_spatial_orbitals
    num_particles = problem.num_particles
    assert num_spatial_orbitals is not None
    assert num_particles is not None
    ansatz = UCCSD(
        num_spatial_orbitals,
        num_particles,
        mapper,
        initial_state=initial_state,
    )
    log.info(
        "Problem ready: %d spatial orbitals, %d particles → %d-qubit UCCSD ansatz",
        num_spatial_orbitals,
        num_particles[0] + num_particles[1],
        ansatz.num_qubits,
    )

    if backend.num_qubits < ansatz.num_qubits:
        sys.exit(
            f"Selected backend exposes {backend.num_qubits} qubits, but the QSCI example needs {ansatz.num_qubits}."
        )

    log.info("Transpiling UCCSD ansatz for '%s'...", backend.name)
    ansatz = transpile(ansatz, backend=backend)
    log.info(
        "Transpiled ansatz: %d qubits, %d gates, depth %d",
        ansatz.num_qubits,
        ansatz.size(),
        ansatz.depth(),
    )

    hamiltonian = problem.hamiltonian
    second_q_operator = hamiltonian.second_q_op()
    observable = cast("SparsePauliOp", mapper.map(second_q_operator))
    log.info(
        "Mapped Hamiltonian to %d-term qubit observable.",
        len(observable),
    )

    log.info("Running VQE (optimizer=L-BFGS-B, maxiter=%d, shots=%d)...", args.maxiter, args.shots)
    estimator = QDMIEstimator(backend, options={"default_shots": args.shots})
    vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=args.maxiter))
    result = vqe.compute_minimum_eigenvalue(operator=observable)
    optimal_parameters = result.optimal_parameters
    if optimal_parameters is None:
        sys.exit("VQE must return optimal parameters.")
    log.info(
        "VQE converged. Eigenvalue estimate: %.6f (function evaluations: %s)",
        result.eigenvalue,
        result.optimizer_evals,
    )

    log.info("Assigning optimal parameters and adding measurements to ansatz...")
    ansatz.assign_parameters(optimal_parameters, inplace=True)
    ansatz.measure_active()

    log.info("Submitting sampling job to '%s' (%d shots)...", backend.name, args.shots)
    sampler = QDMISampler(backend, default_shots=args.shots)
    job = sampler.run([(ansatz,)])
    counts = job.result()[0].data["meas"].get_counts()
    log.info("Job completed. Collected %d shots across %d distinct bitstrings.", sum(counts.values()), len(counts))

    log.info("Running QSCI post-processing (cutoff=%d)...", args.cutoff)
    eigval = _postprocess_counts(ATOM, BASIS, counts, cutoff=args.cutoff)
    qsci_energy = eigval + (problem.hamiltonian.nuclear_repulsion_energy or 0.0)
    energy_difference = abs(EXPECTED_ENERGY - qsci_energy)

    log.info("QSCI total energy: %.6f Ha", qsci_energy)
    log.info(
        "Absolute energy difference from expected (%.6f Ha): %.6f Ha",
        EXPECTED_ENERGY,
        energy_difference,
    )
    log.info("Done.")


def _full_mbh_canonical_from_qsci(
    basis: list[int],
    h_diag: np.ndarray,
    h_off_diag: np.ndarray,
    u_full: np.ndarray,
    num_qubits: int,
    tol: float = 1e-4,
) -> tuple[list[int], list[int], list[float]]:
    """Calculate a sparse Hamiltonian representation for the selected QSCI basis.

    Args:
        basis: Bitstring basis states kept for the reduced Hamiltonian.
        h_diag: Diagonal one-body contributions in the spin-orbital basis.
        h_off_diag: Off-diagonal one-body contributions in the spin-orbital basis.
        u_full: Two-body interaction tensor in chemist notation.
        num_qubits: Number of spin orbitals in the encoded problem.
        tol: Numerical threshold used to discard negligible terms.

    Returns:
        Sparse matrix triplets as row indices, column indices, and data values.
    """
    row, col, data = [], [], []

    for index, state in enumerate(basis):
        occupations = [int(digit) for digit in f"{state:0{num_qubits}b}"]
        occupations_up = occupations[: num_qubits // 2]
        occupations_down = occupations[num_qubits // 2 :]

        diagonal_interaction = sum(
            u_full[orbital, orbital, partner, partner] * occupations_up[orbital] * occupations_down[partner]
            for orbital in range(num_qubits // 2)
            for partner in range(num_qubits // 2)
        )
        diagonal_value = sum(occupations * h_diag) + diagonal_interaction
        if abs(diagonal_value) > tol:
            row.append(index)
            col.append(index)
            data.append(float(diagonal_value))

        for column_index in range(index + 1, len(basis)):
            differing_bits = f"{state ^ basis[column_index]:0{num_qubits}b}"
            bit_indices = [offset for offset, bit in enumerate(differing_bits) if bit == "1"]

            if len(bit_indices) == 2:
                first, second = bit_indices
                if first < num_qubits // 2 and second < num_qubits // 2:
                    off_diagonal_value = h_off_diag[first, second] + sum(
                        u_full[first, second, partner, partner] * occupations[partner + num_qubits // 2]
                        for partner in range(num_qubits // 2)
                    )
                elif first >= num_qubits // 2 and second >= num_qubits // 2:
                    off_diagonal_value = h_off_diag[first - num_qubits // 2, second - num_qubits // 2] + sum(
                        u_full[partner, partner, first - num_qubits // 2, second - num_qubits // 2]
                        * occupations[partner]
                        for partner in range(num_qubits // 2)
                    )
                else:
                    continue
                if abs(off_diagonal_value) > tol:
                    sign = (-1) ** sum(occupations[min(first, second) + 1 : max(first, second)])
                    row.extend((index, column_index))
                    col.extend((column_index, index))
                    data.extend((float(sign * off_diagonal_value), float(sign * off_diagonal_value)))

            elif len(bit_indices) == 4:
                first, second, third, fourth = bit_indices
                if first < num_qubits // 2 <= third and second < num_qubits // 2 <= fourth:
                    off_diagonal_value = u_full[first, second, third - num_qubits // 2, fourth - num_qubits // 2]
                    if abs(off_diagonal_value) > tol:
                        sign_up = (-1) ** sum(occupations[first + 1 : second])
                        sign_down = (-1) ** sum(occupations[third + 1 : fourth])
                        sign = sign_up * sign_down
                        row.extend((index, column_index))
                        col.extend((column_index, index))
                        data.extend((float(sign * off_diagonal_value), float(sign * off_diagonal_value)))

    return row, col, data


def _postprocess_counts(atom: str, basis: str, counts: dict[str, int], *, cutoff: int = 10) -> float:
    """Postprocess sampled bitstrings into a QSCI energy estimate.

    Args:
        atom: Molecular geometry specification passed to PySCF.
        basis: Basis-set label passed to PySCF.
        counts: Sampled computational-basis bitstring counts.
        cutoff: Maximum number of basis states to keep during reconstruction.

    Returns:
        The estimated ground-state electronic energy.

    Raises:
        ValueError: If no valid states are available after filtering the sampled counts.
    """
    if not counts:
        msg = "counts must not be empty"
        raise ValueError(msg)

    basis_states = list(counts.keys())
    basis_counts = list(counts.values())
    num_qubits = len(basis_states[0])
    sorted_indices = np.argsort(basis_counts)[::-1]

    selected_states = []
    for index in sorted_indices[: min(cutoff, len(basis_states)) * 2]:
        state = basis_states[index]
        if state.count("1") == num_qubits // 2 and (
            state[: num_qubits // 2].count("1") == state[num_qubits // 2 :].count("1")
        ):
            selected_states.append(int(state, 2))
        if len(selected_states) >= min(cutoff, len(basis_states)):
            break

    if not selected_states:
        msg = "no states selected after occupancy filtering"
        raise ValueError(msg)

    molecule = gto.Mole(verbose=0)
    molecule.build(atom=atom, basis=basis)
    mean_field = scf.RHF(molecule).run()
    core_hamiltonian = mean_field.get_hcore()
    mean_field.kernel()
    molecular_orbitals = mean_field.mo_coeff
    orthogonalized_core = reduce(np.dot, (molecular_orbitals.conj().T, core_hamiltonian, molecular_orbitals))
    orthogonalized_eri = ao2mo.kernel(mean_field._eri, molecular_orbitals)  # noqa: SLF001
    diagonal_terms = np.kron(np.array([1, 1]), np.diag(orthogonalized_core))
    off_diagonal_terms = orthogonalized_core - np.diag(np.diag(orthogonalized_core))
    restored_eri = ao2mo.restore(1, orthogonalized_eri, molecule.nao)

    rows, cols, values = _full_mbh_canonical_from_qsci(
        selected_states,
        diagonal_terms,
        off_diagonal_terms,
        restored_eri,
        num_qubits,
    )
    reduced_hamiltonian: csr_matrix = csr_matrix(
        (values, (rows, cols)),
        shape=(len(selected_states), len(selected_states)),
    )

    if len(selected_states) < 3:
        eigenvalues = np.real(sla.eigvals(reduced_hamiltonian.toarray()))
        eigenvalues.sort()
        return float(eigenvalues[0])

    eigenvalues, _ = eigs(reduced_hamiltonian, k=1, which="SR")
    return float(np.real(eigenvalues[0]))


if __name__ == "__main__":
    main()
