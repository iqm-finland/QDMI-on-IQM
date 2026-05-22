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

"""Run Grover's algorithm using the QDMI-on-IQM stack."""

from __future__ import annotations

import argparse
import logging
import sys

import numpy as np
from mqt.bench import BenchmarkLevel, get_benchmark
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit.quantum_info import hellinger_fidelity

from iqm.qdmi.qiskit import IQMBackend

log = logging.getLogger(__name__)

if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S",
    )

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--shots", type=int, default=8192)
    parser.add_argument("--num-qubits", type=int, default=7)
    args = parser.parse_args()
    log.info(
        "Starting Grover example (backend=%s, qubits=%d, shots=%d)",
        args.backend,
        args.num_qubits,
        args.shots,
    )

    log.info("Initialising '%s' backend...", args.backend)
    backend = IQMBackend() if args.backend == "iqm" else QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
    log.info("Backend ready: '%s' | %d qubits", backend.name, backend.num_qubits)

    if backend.num_qubits < args.num_qubits:
        sys.exit(
            f"Selected backend exposes {backend.num_qubits} qubits, but the Grover example needs {args.num_qubits}."
        )

    log.info("Building and mapping Grover benchmark circuit (n=%d)...", args.num_qubits)
    circuit = get_benchmark(
        benchmark="grover",
        level=BenchmarkLevel.MAPPED,
        circuit_size=args.num_qubits,
        target=backend.target,
    )
    log.info("Circuit ready: %d qubits, %d gates, depth %d", circuit.num_qubits, circuit.size(), circuit.depth())

    log.info("Submitting job to '%s' (%d shots)...", backend.name, args.shots)
    sampler = QDMISampler(backend, default_shots=args.shots)
    job = sampler.run([(circuit,)])
    counts: dict[str, int] = job.result()[0].data["meas"].get_counts()
    total_shots = sum(counts.values())
    log.info("Job completed. Collected %d shots across %d distinct bitstrings.", total_shots, len(counts))
    log.info("Measured counts: %s", sorted(counts.items()))

    actual_qubits = args.num_qubits - 1
    r = int(np.pi / 4 * np.sqrt(2**actual_qubits))
    theta = 2 * np.arcsin(1 / np.sqrt(2**actual_qubits))
    success_prob = float(np.sin((r + 0.5) * theta) ** 2)
    expected_counts = {
        "1" * args.num_qubits: success_prob * args.shots,
    }
    if success_prob < 1:
        remaining_prob = 1 - success_prob
        num_remaining_bitstrings = 2**actual_qubits - 1
        remaining_prob_per_bitstring = remaining_prob * args.shots / num_remaining_bitstrings
        for i in range(2**actual_qubits):
            bitstring = format(i, f"0{actual_qubits}b")
            if bitstring != "1" * actual_qubits:
                expected_counts["1" + bitstring] = remaining_prob_per_bitstring

    fidelity = hellinger_fidelity(counts, expected_counts)
    log.info("Hellinger fidelity between measured distribution and ideal distribution: %.7f", fidelity)
    log.info("Done.")
