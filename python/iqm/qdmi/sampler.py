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

"""Command line interface for sampling Qiskit circuits on IQM devices."""

from __future__ import annotations

import argparse
import json
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import TYPE_CHECKING, Any, cast

import numpy as np

try:
    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from mqt.core.plugins.qiskit.sampler import QDMISampler
    from qiskit import qpy, transpile
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from collections.abc import Sequence


def _serialize_value(val: object) -> object:
    if hasattr(val, "get_counts") and callable(getattr(val, "get_counts", None)):
        return cast("Any", val).get_counts()
    if isinstance(val, np.ndarray):
        return val.tolist()  # ty: ignore[no-matching-overload]
    if isinstance(val, (np.integer, np.floating)):
        return val.item()
    if isinstance(val, Mapping) or (hasattr(val, "items") and callable(getattr(val, "items", None))):
        return {str(k): _serialize_value(v) for k, v in cast("Any", val).items()}
    if isinstance(val, list):
        return [_serialize_value(v) for v in val]
    return val


def _serialize_primitive_result(result: object) -> dict:
    iterator = result if isinstance(result, Iterable) else [result]

    serialized_pubs = [
        {
            "data": _serialize_value(getattr(pub, "data", pub)),
            "metadata": _serialize_value(getattr(pub, "metadata", {})),
        }
        for pub in iterator
    ]
    return {
        "results": serialized_pubs,
        "metadata": _serialize_value(getattr(result, "metadata", {})),
    }


def main(argv: Sequence[str] | None = None) -> None:
    """Execute the sampler CLI subcommand."""
    parser = argparse.ArgumentParser(description="Sample a serialized QPY circuit.")
    parser.add_argument("circuit", type=str, help="Path to the QPY circuit file.")
    parser.add_argument("--shots", type=int, required=True, help="Number of shots to run the circuit.")
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
        circuit = qpy.load(file_obj)[0]

    if args.simulator:
        backend = QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
        sampler = QDMISampler(backend)
    else:
        backend = IQMBackend(
            base_url=args.base_url,
            tokens_file=args.tokens_file,
            qc_id=args.qc_id,
            qc_alias=args.qc_alias,
        )
        sampler = backend.sampler()

    circuit_for_execution = transpile(circuit, backend)
    job = sampler.run([(circuit_for_execution,)], shots=args.shots)
    serialized = _serialize_primitive_result(job.result())
    print(json.dumps(serialized))


if __name__ == "__main__":
    main()
