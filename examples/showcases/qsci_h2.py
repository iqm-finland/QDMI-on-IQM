#!/usr/bin/env -S uv run --script --quiet
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

# /// script
# requires-python = ">=3.10,<3.14"
# dependencies = [
#   "iqm-qdmi",
#   "mqt-core[qiskit]~=3.6.0",
#   "qiskit>=1.1,<2.0",
#   "qiskit-algorithms>=0.3.0,<0.4.0",
#   "qiskit-nature>=0.7.2",
#   "pyscf>=2.11.0; platform_system != 'Windows'",
# ]
# ///

"""Run an H2 QSCI showcase through the IQM QDMI Qiskit backend."""

from __future__ import annotations

import argparse
import os
import platform
import sys
from pathlib import Path
from typing import TYPE_CHECKING

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit import transpile
from qiskit.primitives import BackendEstimator
from qiskit.quantum_info import SparsePauliOp
from qiskit_algorithms import VQE, NumPyMinimumEigensolver
from qiskit_algorithms.optimizers import L_BFGS_B
from qiskit_nature.second_q.algorithms import GroundStateEigensolver
from qiskit_nature.second_q.algorithms.initial_points import MP2InitialPoint
from qiskit_nature.second_q.circuit.library import UCCSD, HartreeFock
from qiskit_nature.second_q.drivers import PySCFDriver
from qiskit_nature.second_q.mappers import JordanWignerMapper
from qiskit_nature.second_q.operators import FermionicOp

from examples.showcases import qsci_postprocess
from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from qiskit.circuit import QuantumCircuit

ATOM = "H 0 0 0; H 0 0 1;"
BASIS = "sto-3g"


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments for the QSCI showcase.

    Returns:
        Parsed command-line arguments.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--shots", type=int, default=2048)
    parser.add_argument("--maxiter", type=int, default=30)
    parser.add_argument("--cutoff", type=int, default=10)
    parser.add_argument("--energy-tolerance", type=float, default=0.35)
    return parser.parse_args()


def make_backend(backend_kind: str) -> QDMIBackend:
    """Create the selected QDMI backend for the showcase run.

    Args:
        backend_kind: Backend mode selected on the command line.

    Returns:
        The requested QDMI backend.

    Raises:
        SystemExit: If required IQM credentials are missing or no simulator backend is available.
    """
    if backend_kind == "iqm":
        token = os.getenv("IQM_TOKEN") or os.getenv("RESONANCE_API_KEY")
        tokens_file = os.getenv("IQM_TOKENS_FILE")
        if token is None and tokens_file is None:
            msg = "Set RESONANCE_API_KEY, IQM_TOKEN, or IQM_TOKENS_FILE to run this showcase on IQM hardware."
            raise SystemExit(msg)

        return IQMBackend(
            base_url=os.getenv("IQM_BASE_URL"),
            token=token,
            tokens_file=tokens_file,
            qc_id=os.getenv("IQM_QC_ID"),
            qc_alias=os.getenv("IQM_QC_ALIAS"),
        )

    provider = QDMIProvider()
    try:
        return provider.get_backend("MQT Core DDSIM QDMI Device")
    except ValueError:
        simulator_backends = provider.backends(name="DDSIM")
        if len(simulator_backends) == 1:
            return simulator_backends[0]
        if not simulator_backends:
            msg = "No QDMI simulator backend is available through QDMIProvider()."
            raise SystemExit(msg) from None

        backend_names = ", ".join(backend.name or "<unnamed>" for backend in simulator_backends)
        msg = f"Multiple simulator backends matched this showcase: {backend_names}."
        raise SystemExit(msg) from None


def transpile_for_backend(circuit: QuantumCircuit, backend: QDMIBackend, backend_kind: str) -> QuantumCircuit:
    """Transpile a circuit for the selected backend kind.

    Args:
        circuit: Circuit to transpile.
        backend: Backend used for backend-aware transpilation.
        backend_kind: Backend mode selected on the command line.

    Returns:
        The transpiled circuit.
    """
    if backend_kind == "sim":
        return transpile(circuit, basis_gates=["u", "cx", "measure"], optimization_level=1)
    return transpile(circuit, backend=backend, optimization_level=3)


def main() -> None:
    """Execute the H2 QSCI showcase.

    Raises:
        SystemExit: If backend setup fails or the sampled energy deviates beyond the accepted threshold.
    """
    if platform.system() == "Windows":
        msg = "QSCI requires PySCF, which is not supported on Windows."
        raise SystemExit(msg)

    args = parse_args()
    backend = make_backend(args.backend)

    driver = PySCFDriver(atom=ATOM, basis=BASIS)
    problem = driver.run()
    if problem.num_spatial_orbitals is None:
        msg = "num_spatial_orbitals is required"
        raise SystemExit(msg)
    if problem.num_particles is None:
        msg = "num_particles is required"
        raise SystemExit(msg)
    if problem.nuclear_repulsion_energy is None:
        msg = "nuclear_repulsion_energy is required"
        raise SystemExit(msg)

    mapper = JordanWignerMapper()
    initial_state = HartreeFock(problem.num_spatial_orbitals, problem.num_particles, mapper)
    ansatz = UCCSD(
        problem.num_spatial_orbitals,
        problem.num_particles,
        mapper,
        initial_state=initial_state,
    )
    second_q_operator = problem.second_q_ops()[0]
    if not isinstance(second_q_operator, FermionicOp):
        msg = "The QSCI showcase requires a fermionic operator."
        raise SystemExit(msg)
    observable = mapper.map(second_q_operator)
    if not isinstance(observable, SparsePauliOp):
        msg = "Mapped observable must be a SparsePauliOp."
        raise SystemExit(msg)
    if backend.num_qubits < ansatz.num_qubits:
        msg = f"Selected backend exposes {backend.num_qubits} qubits, but the QSCI showcase needs {ansatz.num_qubits}."
        raise SystemExit(msg)

    vqe_ansatz = transpile_for_backend(ansatz, backend, args.backend) if args.backend == "sim" else ansatz
    mp2_initial_point = MP2InitialPoint()
    mp2_initial_point.compute(ansatz=ansatz, problem=problem)
    # qiskit_algorithms.VQE still expects the Estimator V1 interface.
    estimator = BackendEstimator(
        backend=backend,
        options={"shots": args.shots},
        skip_transpilation=args.backend == "sim",
    )
    vqe = VQE(
        estimator,
        vqe_ansatz,
        L_BFGS_B(maxiter=args.maxiter),
        initial_point=mp2_initial_point.to_numpy_array(),
    )
    result = vqe.compute_minimum_eigenvalue(operator=observable)
    if result.optimal_parameters is None:
        msg = "VQE must return optimal parameters."
        raise SystemExit(msg)

    trained_ansatz = vqe_ansatz.assign_parameters(result.optimal_parameters)
    if trained_ansatz is None:
        msg = "assign_parameters must return a bound circuit."
        raise SystemExit(msg)

    sample_circuit = trained_ansatz.copy()
    sample_circuit.measure_all()
    transpiled_sample = transpile_for_backend(sample_circuit, backend, args.backend)
    sampler = QDMISampler(backend, default_shots=args.shots)
    job = sampler.run([(transpiled_sample,)])
    data = job.result()[0].data
    counts = data["meas"].get_counts() if "meas" in data else next(iter(data.values())).get_counts()
    if sum(counts.values()) != args.shots:
        msg = f"Expected {args.shots} shots, but observed {sum(counts.values())}."
        raise SystemExit(msg)

    eigval = qsci_postprocess.postprocess_counts(ATOM, BASIS, counts, cutoff=args.cutoff)
    qsci_energy = eigval + problem.nuclear_repulsion_energy

    exact_solver = GroundStateEigensolver(mapper, NumPyMinimumEigensolver())
    exact_result = exact_solver.solve(problem)
    exact_energies = getattr(exact_result, "total_energies", None)
    if exact_energies is None:
        msg = "The exact solver must return total energies."
        raise SystemExit(msg)
    exact_energy = float(exact_energies[0])
    energy_difference = abs(exact_energy - qsci_energy)

    if energy_difference >= args.energy_tolerance:
        msg = (
            f"QSCI energy deviated from the exact reference by {energy_difference:.6f}, "
            f"expected less than {args.energy_tolerance:.6f}."
        )
        raise SystemExit(msg)


if __name__ == "__main__":
    main()
