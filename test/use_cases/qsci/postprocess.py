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

"""Classical postprocessing helpers for the QSCI use-case workflow."""

from __future__ import annotations

from functools import reduce

import numpy as np
import scipy.linalg as sla
from pyscf import ao2mo, gto, scf
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigs


def _full_mbh_canonical_from_qsci(
    basis: list[int],
    h_diag: np.ndarray,
    h_off_diag: np.ndarray,
    u_full: np.ndarray,
    num_qubits: int,
    tol: float = 1e-4,
) -> tuple[list[int], list[int], list[float]]:
    """Calculate a sparse Hamiltonian representation for the selected QSCI basis.

    Returns:
        The sparse Hamiltonian encoded as row indices, column indices, and values.
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


def postprocess_counts(atom: str, basis: str, counts: dict[str, int], *, cutoff: int = 10) -> float:
    """Postprocess sampled bitstrings into a QSCI energy estimate.

    Returns:
        The estimated electronic ground-state energy from the selected configurations.
    """
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
