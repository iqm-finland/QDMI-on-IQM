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
# requires-python = ">=3.10"
# dependencies = [
#   "iqm-qdmi",
#   "mqt-core[qiskit]~=3.6.0",
#   "mqt-bench>=2.0.0",
# ]
# ///

"""Run a W-state example through the IQM QDMI Qiskit backend."""

from __future__ import annotations

import argparse
import os
from typing import TYPE_CHECKING

from mqt.bench import BenchmarkLevel, get_benchmark
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit import transpile
from qiskit.quantum_info import hellinger_fidelity

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments for the W-state example.

    Returns:
        Parsed command-line arguments.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--shots", type=int, default=1024)
    parser.add_argument("--num-qubits", type=int, default=3)
    return parser.parse_args()


def make_backend(backend_kind: str) -> QDMIBackend:
    """Create the selected QDMI backend for the example run.

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
            msg = "Set RESONANCE_API_KEY, IQM_TOKEN, or IQM_TOKENS_FILE to run this example on IQM hardware."
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
        msg = f"Multiple simulator backends matched this example: {backend_names}."
        raise SystemExit(msg) from None


def main() -> None:
    """Execute the W-state example.

    Raises:
        SystemExit: If backend setup fails or the backend returns an unexpected shot count.
    """
    args = parse_args()
    backend = make_backend(args.backend)
    if backend.num_qubits < args.num_qubits:
        msg = f"Selected backend exposes {backend.num_qubits} qubits, but the W-state example needs {args.num_qubits}."
        raise SystemExit(msg)

    circuit = get_benchmark(
        benchmark="wstate",
        level=BenchmarkLevel.ALG,
        circuit_size=args.num_qubits,
    )
    if not any(instruction.operation.name == "measure" for instruction in circuit.data):
        circuit.measure_all()

    if args.backend == "sim":
        transpiled_circuit = transpile(circuit, basis_gates=["u", "cx", "measure"], optimization_level=1)
    else:
        transpiled_circuit = transpile(circuit, backend=backend, optimization_level=3)

    sampler = QDMISampler(backend, default_shots=args.shots)
    job = sampler.run([(transpiled_circuit,)])
    data = job.result()[0].data
    counts = data["meas"].get_counts() if "meas" in data else next(iter(data.values())).get_counts()
    total_shots = sum(counts.values())
    if total_shots != args.shots:
        msg = f"Expected {args.shots} shots, but observed {total_shots}."
        raise SystemExit(msg)

    expected_states = ["0" * index + "1" + "0" * (args.num_qubits - index - 1) for index in range(args.num_qubits)]
    expected_counts = dict.fromkeys(expected_states, args.shots // args.num_qubits)
    remaining_shots = args.shots - sum(expected_counts.values())
    for state in expected_states[:remaining_shots]:
        expected_counts[state] += 1
    fidelity = hellinger_fidelity(counts, expected_counts)

    print(f"Measured counts: {dict(sorted(counts.items()))}")
    print(f"Hellinger fidelity to the ideal W-state distribution: {fidelity:.2%}")


if __name__ == "__main__":
    main()
