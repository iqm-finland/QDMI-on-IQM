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
# requires-python = ">=3.11"
# dependencies = ["iqm-qdmi[qiskit]"]
#
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# ///

"""Run a Max-Cut QAOA workload using the QDMI-on-IQM stack."""

from __future__ import annotations

import argparse
import logging
from typing import TYPE_CHECKING

import numpy as np
from qiskit.circuit.library import qaoa_ansatz
from qiskit.compiler import transpile
from qiskit.quantum_info import SparsePauliOp
from qiskit_algorithms.optimizers import COBYLA
from showcase_backend import add_backend_arguments, showcase_backend, validate_backend_arguments

if TYPE_CHECKING:
    from numpy.typing import NDArray

log = logging.getLogger(__name__)
_EDGES = ((0, 1), (1, 2), (2, 3), (3, 0))
_NUM_QUBITS = 4


def main() -> None:
    """Optimize a depth-one Max-Cut circuit using sampled cuts."""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    add_backend_arguments(parser)
    parser.add_argument("--shots", type=int, default=256)
    parser.add_argument("--maxiter", type=int, default=20, help="Maximum number of COBYLA objective evaluations.")
    args = parser.parse_args()
    validate_backend_arguments(parser, args)
    if args.shots <= 0:
        parser.error("--shots must be positive")
    if args.maxiter < 4:
        parser.error("--maxiter must be at least 4")

    cost = SparsePauliOp.from_sparse_list([("ZZ", [first, second], 0.5) for first, second in _EDGES], _NUM_QUBITS)
    circuit = qaoa_ansatz(cost, reps=1)
    circuit.measure_all()

    with showcase_backend(args.backend, num_qubits=_NUM_QUBITS, iteration_mode=args.iteration_mode) as backend:
        if backend.num_qubits < _NUM_QUBITS:
            parser.error(f"The QAOA example requires {_NUM_QUBITS} qubits.")
        native = transpile(circuit, backend)
        sampler = backend.sampler(default_shots=args.shots)

        def evaluate(parameters: NDArray[np.float64]) -> float:
            counts = sampler.run([(native, parameters)]).result()[0].data["meas"].get_counts()
            # Qiskit displays classical bit zero at the right-hand end of a count key.
            return -sum(
                count * sum(bits[-1 - first] != bits[-1 - second] for first, second in _EDGES)
                for bits, count in counts.items()
            ) / sum(counts.values())

        result = COBYLA(maxiter=args.maxiter).minimize(evaluate, np.array([0.3, 0.6]))

    log.info("QAOA estimated cut size %.3f out of 4 (%s objective evaluations).", -result.fun, result.nfev)


if __name__ == "__main__":
    main()
