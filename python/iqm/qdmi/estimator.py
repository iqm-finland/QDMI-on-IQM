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

"""Command line interface for VQE parameter estimation on IQM devices."""

from __future__ import annotations

import argparse
import json
import pickle  # noqa: S403
from collections.abc import Mapping
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

import numpy as np

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


def _serialize_value(val: object) -> object:
    if isinstance(val, np.ndarray):
        return val.tolist()  # ty: ignore[no-matching-overload]
    if isinstance(val, (np.integer, np.floating)):
        return val.item()
    if isinstance(val, Mapping) or (hasattr(val, "items") and callable(val.items)):
        return {str(k): _serialize_value(v) for k, v in cast("Any", val).items()}
    if isinstance(val, list):
        return [_serialize_value(v) for v in val]
    return val


def _serialize_vqe_result(result: object) -> dict:
    optimal_params = getattr(result, "optimal_parameters", None)
    serialized_params = {}
    if isinstance(optimal_params, Mapping):
        for param, val in optimal_params.items():
            param_name = getattr(param, "name", str(param))
            serialized_params[param_name] = float(val)

    eigenvalue = getattr(result, "eigenvalue", None)
    serialized_eigenvalue = None
    if eigenvalue is not None:
        if isinstance(eigenvalue, complex):
            if abs(eigenvalue.imag) < 1e-12:
                serialized_eigenvalue = float(eigenvalue.real)
            else:
                serialized_eigenvalue = {"real": float(eigenvalue.real), "imag": float(eigenvalue.imag)}
        else:
            serialized_eigenvalue = float(eigenvalue)

    optimal_value = getattr(result, "optimal_value", None)
    if isinstance(optimal_value, complex) and abs(optimal_value.imag) < 1e-12:
        optimal_value = float(optimal_value.real)

    return {
        "eigenvalue": serialized_eigenvalue,
        "optimal_parameters": serialized_params,
        "optimal_point": _serialize_value(getattr(result, "optimal_point", None)),
        "optimal_value": optimal_value,
        "optimizer_time": getattr(result, "optimizer_time", None),
        "cost_function_evals": getattr(result, "cost_function_evals", None),
    }


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
    print(json.dumps(_serialize_vqe_result(result)))


if __name__ == "__main__":
    main()
