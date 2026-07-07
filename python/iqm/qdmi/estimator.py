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

"""Command line interface for VQE parameter estimation on IQM devices."""

from __future__ import annotations

import argparse
import base64
import pickle  # noqa: S403
from pathlib import Path
from typing import TYPE_CHECKING

try:
    from mqt.core.plugins.qiskit.estimator import QDMIEstimator
    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from qiskit import qpy
    from qiskit_algorithms import VQE
    from qiskit_algorithms.optimizers import L_BFGS_B
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin and VQE requirements. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from collections.abc import Sequence

    from qiskit.quantum_info import SparsePauliOp


def main(argv: Sequence[str] | None = None) -> None:
    """Execute the estimator CLI subcommand."""
    parser = argparse.ArgumentParser(description="Estimate VQE parameters for a serialized ansatz.")
    parser.add_argument("circuit", type=str, help="Path to the QPY circuit file.")
    parser.add_argument("observable", type=str, help="Path to the serialized observable file.")
    parser.add_argument("--maxiter", type=int, required=True, help="Maximum number of iterations.")
    parser.add_argument("--timeout", type=int, help="Timeout passed to the IQM Backend in seconds.", default=300)
    parser.add_argument("--simulator", action="store_true", help="Use the simulator instead of the actual backend.")
    parser.add_argument("--base-url", type=str, dest="base_url", help="IQM server base URL.", default=None)
    parser.add_argument("--tokens-file", type=str, help="IQM tokens file for authentication.", default=None)
    parser.add_argument("--qc-id", type=str, dest="qc_id", help="Quantum computer ID to use.", default=None)
    parser.add_argument(
        "--qc-alias",
        type=str,
        dest="qc_alias",
        help="Quantum computer alias to use (e.g., 'emerald:mock').",
        default=None,
    )

    args = parser.parse_args(list(argv) if argv is not None else None)

    with Path(args.circuit).open("rb") as file_obj:
        ansatz = qpy.load(file_obj)[0]

    with Path(args.observable).open("rb") as file_obj:
        observable: SparsePauliOp = pickle.load(file_obj)  # noqa: S301

    if args.simulator:
        backend = QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
        estimator = QDMIEstimator(backend)
    else:
        backend = IQMBackend(
            base_url=args.base_url,
            tokens_file=args.tokens_file,
            qc_id=args.qc_id,
            qc_alias=args.qc_alias,
        )
        estimator = backend.estimator()

    vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=args.maxiter))
    result = vqe.compute_minimum_eigenvalue(operator=observable)
    pickled = pickle.dumps(result)
    print(base64.b64encode(pickled).decode("utf-8"))


if __name__ == "__main__":
    main()
