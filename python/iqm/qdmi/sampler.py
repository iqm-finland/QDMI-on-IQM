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
import contextlib
import os
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import TYPE_CHECKING, Protocol, cast

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
    from collections.abc import Iterator, Sequence
    from typing import SupportsInt

    from mqt.core.plugins.qiskit.backend import QDMIBackend


class _SupportsResult(Protocol):
    """Protocol for jobs exposing a result method."""

    def result(self) -> _SupportsIterableResult:
        """Return job results."""


class _SupportsIterableResult(Protocol):
    """Protocol for iterable sampler/estimator v2 results."""

    def __iter__(self) -> Iterator[_SupportsPubResult]:
        """Iterate pub results."""


class _SupportsPubResult(Protocol):
    """Protocol for a single pub result."""

    data: _SupportsDataBin
    metadata: object


class _SupportsDataBin(Protocol):
    """Protocol for pub result data containers."""

    def __getitem__(self, key: str) -> object:
        """Read a field from the data bin."""


class _SupportsSamplerPrimitive(Protocol):
    """Protocol for sampler primitives with a run method."""

    def run(self, pubs: list[tuple[object, ...]], shots: int) -> _SupportsResult:
        """Execute sampler pubs and return a job handle."""


def build_simulator_backend() -> QDMIBackend:
    """Create the MQT Core DDSIM backend used for simulation.

    Returns:
        The MQT Core QDMI simulator backend.
    """
    return QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")


def _count_to_int(count: object) -> int:
    """Convert an external count payload to a plain integer.

    Returns:
        The converted count value.
    """
    return int(cast("SupportsInt | str | bytes | bytearray", count))


def normalize_counts(raw_counts: object) -> dict[str, int]:
    """Convert backend count payloads into a plain typed dictionary.

    Returns:
        A normalized mapping from bitstrings to integer counts.
    """
    if isinstance(raw_counts, Mapping):
        return {str(bitstring): _count_to_int(count) for bitstring, count in raw_counts.items()}

    pairs = cast("Iterable[tuple[object, object]]", raw_counts)
    return {str(bitstring): _count_to_int(count) for bitstring, count in pairs}


def extract_counts(pub_result: _SupportsPubResult) -> dict[str, int]:
    """Extract counts from the first classical register exposed by a primitive result.

    Returns:
        A normalized mapping from measured bitstrings to shot counts.

    Raises:
        RuntimeError: If no classical register with counts is present in the result.
    """
    data = pub_result.data

    if isinstance(data, dict):
        values: Iterable[object] = data.values()
    else:
        keys = getattr(data, "keys", None)
        values = (data[key] for key in keys()) if callable(keys) else ()

    for value in values:
        get_counts = getattr(value, "get_counts", None)
        if callable(get_counts):
            return normalize_counts(get_counts())

    for key in ("meas", "c"):
        with contextlib.suppress(AttributeError, KeyError, TypeError):
            value = data[key]
            get_counts = getattr(value, "get_counts", None)
            if callable(get_counts):
                return normalize_counts(get_counts())

    msg = "Could not find a classical register with counts in the primitive result."
    raise RuntimeError(msg)


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
        backend = build_simulator_backend()
        circuit_for_execution = circuit
        sampler = cast("_SupportsSamplerPrimitive", QDMISampler(backend))
    else:
        backend = IQMBackend(
            base_url=args.base_url,
            tokens_file=args.tokens_file,
            qc_id=args.qc_id,
            qc_alias=args.qc_alias,
        )
        circuit_for_execution = transpile(circuit, backend)
        sampler = cast("_SupportsSamplerPrimitive", backend.sampler())

    job = sampler.run([(circuit_for_execution,)], shots=args.shots)
    first_pub = next(iter(job.result()))
    counts = extract_counts(first_pub)
    print(counts)


if __name__ == "__main__":
    main()
