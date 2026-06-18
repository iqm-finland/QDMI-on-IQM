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
#   "iqm-qdmi[qiskit]",
#   "mqt-bench>=2.2.2",
# ]
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# ///

"""Run an MQT Bench workload using the QDMI-on-IQM stack."""

from __future__ import annotations

import argparse
import logging
import sys
from dataclasses import dataclass
from typing import TYPE_CHECKING

import numpy as np
from mqt.bench import BenchmarkLevel, get_benchmark
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit.quantum_info import hellinger_fidelity

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend

log = logging.getLogger(__name__)


@dataclass(frozen=True)
class BenchmarkConfig:
    """Configuration for one benchmark family."""

    benchmark: str
    title: str
    default_shots: int
    default_qubits: int
    result_register: str
    description: str


BENCHMARKS: dict[str, BenchmarkConfig] = {
    "ghz": BenchmarkConfig(
        benchmark="ghz",
        title="GHZ",
        default_shots=1024,
        default_qubits=3,
        result_register="meas",
        description="GHZ state preparation for multi-qubit entanglement checks.",
    ),
    "dj": BenchmarkConfig(
        benchmark="dj",
        title="Deutsch-Jozsa",
        default_shots=1024,
        default_qubits=4,
        result_register="c",
        description="Deutsch-Jozsa oracle sampling.",
    ),
    "qft": BenchmarkConfig(
        benchmark="qft",
        title="QFT",
        default_shots=1024,
        default_qubits=3,
        result_register="meas",
        description="Quantum Fourier Transform sampling.",
    ),
    "graphstate": BenchmarkConfig(
        benchmark="graphstate",
        title="Graph State",
        default_shots=1024,
        default_qubits=4,
        result_register="meas",
        description="Graph-state preparation and sampling.",
    ),
    "wstate": BenchmarkConfig(
        benchmark="wstate",
        title="W State",
        default_shots=1024,
        default_qubits=3,
        result_register="meas",
        description="W-state sampling.",
    ),
    "grover": BenchmarkConfig(
        benchmark="grover",
        title="Grover",
        default_shots=8192,
        default_qubits=7,
        result_register="meas",
        description="Grover search sampling.",
    ),
    "qpe": BenchmarkConfig(
        benchmark="qpeexact",
        title="Quantum Phase Estimation",
        default_shots=8192,
        default_qubits=5,
        result_register="c",
        description="Quantum Phase Estimation sampling.",
    ),
}


def _build_backend(backend_name: str) -> QDMIBackend:
    if backend_name == "iqm":
        return IQMBackend()
    return QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")


def _describe_result(key: str, counts: dict[str, int], num_qubits: int, shots: int) -> str:
    """Summarize the observed result distribution for the selected benchmark.

    Returns:
        A short human-readable summary for the selected benchmark family.
    """
    if key == "ghz":
        expected = {"0" * num_qubits: shots // 2, "1" * num_qubits: shots - shots // 2}
        return f"GHZ fidelity={hellinger_fidelity(counts, expected):.7f}"
    if key == "dj":
        expected = {"1" * (num_qubits - 1): shots}
        return f"Deutsch-Jozsa fidelity={hellinger_fidelity(counts, expected):.7f}"
    if key == "qft":
        expected = {format(i, f"0{num_qubits}b"): shots / (2**num_qubits) for i in range(2**num_qubits)}
        return f"QFT fidelity={hellinger_fidelity(counts, expected):.7f}"
    if key == "graphstate":
        expected = {format(i, f"0{num_qubits}b"): shots / (2**num_qubits) for i in range(2**num_qubits)}
        return f"Graph-state fidelity={hellinger_fidelity(counts, expected):.7f}"
    if key == "wstate":
        expected = {
            f"{1 << (num_qubits - index - 1):0{num_qubits}b}": shots / num_qubits for index in range(num_qubits)
        }
        return f"W-state fidelity={hellinger_fidelity(counts, expected):.7f}"
    if key == "grover":
        actual_qubits = num_qubits - 1
        r = int(np.pi / 4 * np.sqrt(2**actual_qubits))
        theta = 2 * np.arcsin(1 / np.sqrt(2**actual_qubits))
        success_prob = float(np.sin((r + 0.5) * theta) ** 2)
        expected: dict[str, float] = {"1" * num_qubits: success_prob * shots}
        if success_prob < 1:
            remaining_prob = 1 - success_prob
            remaining_prob_per_bitstring = remaining_prob * shots / (2**actual_qubits - 1)
            for index in range(2**actual_qubits):
                bitstring = format(index, f"0{actual_qubits}b")
                if bitstring != "1" * actual_qubits:
                    expected["1" + bitstring] = remaining_prob_per_bitstring
        return f"Grover fidelity={hellinger_fidelity(counts, expected):.7f}"
    ideal_bitstring = max(counts, key=counts.__getitem__)
    expected = {ideal_bitstring: sum(counts.values())}
    return f"QPE fidelity={hellinger_fidelity(counts, expected):.7f}"


def main() -> None:
    """Run one of the MQT Bench showcase circuits."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S",
    )

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", choices=tuple(BENCHMARKS), default="ghz")
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--shots", type=int, default=None)
    parser.add_argument("--num-qubits", type=int, default=None)
    args = parser.parse_args()

    config = BENCHMARKS[args.benchmark]
    shots = args.shots or config.default_shots
    num_qubits = args.num_qubits or config.default_qubits
    log.info(
        "Starting %s example (backend=%s, qubits=%d, shots=%d)",
        config.title,
        args.backend,
        num_qubits,
        shots,
    )

    log.info("Initialising '%s' backend...", args.backend)
    backend = _build_backend(args.backend)
    log.info("Backend ready: '%s' | %d qubits", backend.name, backend.num_qubits)

    if backend.num_qubits < num_qubits:
        sys.exit(
            f"Selected backend exposes {backend.num_qubits} qubits, but the {config.title} example needs {num_qubits}."
        )

    log.info("Building and mapping %s benchmark circuit (n=%d)...", config.title, num_qubits)
    circuit = get_benchmark(
        benchmark=config.benchmark,
        level=BenchmarkLevel.MAPPED,
        circuit_size=num_qubits,
        target=backend.target,
    )
    log.info("Circuit ready: %d qubits, %d gates, depth %d", circuit.num_qubits, circuit.size(), circuit.depth())

    log.info("Submitting job to '%s' (%d shots)...", backend.name, shots)
    sampler = QDMISampler(backend, default_shots=shots)
    job = sampler.run([(circuit,)])
    counts: dict[str, int] = job.result()[0].data[config.result_register].get_counts()
    total_shots = sum(counts.values())
    log.info("Job completed. Collected %d shots across %d distinct bitstrings.", total_shots, len(counts))
    log.info("Measured counts: %s", sorted(counts.items()))
    log.info("Validation summary: %s", _describe_result(args.benchmark, counts, num_qubits, shots))
    log.info("Done.")


if __name__ == "__main__":
    main()
