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
# requires-python = ">=3.10"
# dependencies = [
#   "iqm-qdmi[qiskit]",
#   "requests>=2.31",
# ]
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# ///

"""Discover IQM quantum computers on a server and select one matching a constraint.

QDMI-on-IQM does not currently expose a Python or C++ binding for listing every
quantum computer known to an IQM Server (each QDMI device session is scoped to a
single quantum computer, selected by ID, alias, or "first available" during
session initialization; see docs/usage.md). This example fills that gap for
discovery *scripts* rather than the library: it issues the same
`api/v1/quantum-computers` request the device session already performs
internally, then queries each candidate's public properties (qubit count and,
where exposed, two-qubit gate fidelity) through the regular `IQMBackend`/QDMI
`Target` API to pick one that satisfies a `--min-qubits` constraint.

Future direction: the IQM Server API is known to expose a queue-length /
execution-availability-window signal, described for the "pay-as-you-go queue"
and therefore apparently cloud/Resonance-oriented (its availability and
semantics for on-premise quantum computers are unconfirmed). That signal is
not currently surfaced through QDMI-on-IQM's Python or C++ bindings, so it is
not used here. Once it is exposed through the library, a natural enhancement
to this example would be ranking candidate backends by queue depth in
addition to qubit count and fidelity.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
from pathlib import Path
from typing import Any

import requests
from mqt.core.plugins.qiskit.provider import QDMIProvider

from iqm.qdmi.qiskit import IQMBackend

log = logging.getLogger(__name__)

_SIMULATOR_DEVICE = "MQT Core DDSIM QDMI Device"
_DEFAULT_BASE_URL = "https://resonance.iqm.tech"


def _resolve_base_url(base_url: str | None) -> str:
    return base_url or os.getenv("IQM_BASE_URL") or _DEFAULT_BASE_URL


def _resolve_listing_bearer_token(token: str | None, tokens_file: str | None) -> str | None:
    """Resolve a bearer token for the plain listing request, mirroring `IQMBackend`'s precedence.

    Returns:
        The resolved bearer token, or `None` if no credentials are configured.
    """
    resolved_token = token or os.getenv("IQM_TOKEN")
    if resolved_token:
        return resolved_token

    resolved_tokens_file = tokens_file or os.getenv("IQM_TOKENS_FILE")
    if resolved_tokens_file:
        data = json.loads(Path(resolved_tokens_file).read_text(encoding="utf-8"))
        access_token = data.get("access_token")
        if not access_token:
            sys.exit(f"No 'access_token' field found in tokens file: {resolved_tokens_file}")
        return str(access_token)

    return None


def _list_quantum_computers(base_url: str, bearer_token: str | None) -> list[dict[str, Any]]:
    """Fetch the quantum computers available on an IQM Server.

    Returns:
        The raw `quantum_computers` entries reported by the server.
    """
    url = base_url.rstrip("/") + "/api/v1/quantum-computers"
    headers = {"Authorization": f"Bearer {bearer_token}"} if bearer_token else {}
    response = requests.get(url, headers=headers, timeout=30)
    response.raise_for_status()
    quantum_computers: list[dict[str, Any]] = response.json().get("quantum_computers", [])
    return quantum_computers


def _calibration_summary(backend: IQMBackend) -> str:
    """Summarize two-qubit gate fidelity from the backend's public `Target`, if available.

    Returns:
        A short human-readable calibration-quality summary.
    """
    target = backend.target
    for name in target.operation_names:
        errors = [
            props.error
            for qargs, props in target[name].items()
            if qargs is not None and len(qargs) == 2 and props is not None and props.error is not None
        ]
        if errors:
            mean_fidelity = 1.0 - (sum(errors) / len(errors))
            return f"'{name}' mean 2-qubit fidelity {mean_fidelity:.4f} over {len(errors)} pair(s)"
    return "no two-qubit gate error data exposed by Target"


def _run_simulator(min_qubits: int) -> None:
    log.info("Simulator backend selected: skipping IQM Server discovery.")
    backend = QDMIProvider().get_backend(_SIMULATOR_DEVICE)
    log.info("Backend ready: '%s' | %d qubits", backend.name, backend.num_qubits)

    if backend.num_qubits < min_qubits:
        sys.exit(f"Simulator exposes {backend.num_qubits} qubits, fewer than --min-qubits={min_qubits}.")

    log.info("Calibration quality: n/a (simulator devices are noiseless)")
    log.info("Selected backend: '%s'", backend.name)
    log.info("Done.")


def _run_discovery(*, base_url: str | None, token: str | None, tokens_file: str | None, min_qubits: int) -> None:
    resolved_base_url = _resolve_base_url(base_url)
    log.info("Discovering quantum computers available at '%s'...", resolved_base_url)
    bearer_token = _resolve_listing_bearer_token(token, tokens_file)
    quantum_computers = _list_quantum_computers(resolved_base_url, bearer_token)
    if not quantum_computers:
        sys.exit(f"No quantum computers reported by '{resolved_base_url}'.")

    log.info("Found %d quantum computer(s):", len(quantum_computers))
    for qc in quantum_computers:
        log.info(
            "  - id=%s alias=%s display_name=%s",
            qc.get("id"),
            qc.get("alias"),
            qc.get("display_name"),
        )

    candidates: list[tuple[str, IQMBackend, str]] = []
    for qc in quantum_computers:
        alias = qc.get("alias")
        if not alias:
            log.warning("Skipping quantum computer without an alias: %s", qc)
            continue
        log.info("Querying properties of '%s'...", alias)
        try:
            backend = IQMBackend(base_url=resolved_base_url, token=token, tokens_file=tokens_file, qc_alias=alias)
        except Exception as exc:  # ruff:ignore[blind-except] - keep discovering the remaining candidates
            log.warning("Skipping '%s': failed to query device properties (%s)", alias, exc)
            continue
        quality = _calibration_summary(backend)
        log.info("  '%s': %d qubits, %s", alias, backend.num_qubits, quality)
        candidates.append((alias, backend, quality))

    matching = [candidate for candidate in candidates if candidate[1].num_qubits >= min_qubits]
    if not matching:
        largest = max((candidate[1].num_qubits for candidate in candidates), default=0)
        sys.exit(f"No discovered quantum computer meets --min-qubits={min_qubits} (largest available: {largest}).")

    selected_alias, selected_backend, selected_quality = max(matching, key=lambda candidate: candidate[1].num_qubits)
    log.info(
        "Selected '%s': %d qubits (>= --min-qubits=%d), %s",
        selected_alias,
        selected_backend.num_qubits,
        min_qubits,
        selected_quality,
    )
    log.info("Done.")


def main() -> None:
    """Discover available IQM quantum computers and select one matching a qubit-count constraint."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S",
    )

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--min-qubits", type=int, default=5)
    parser.add_argument("--base-url", default=None, help="Defaults to $IQM_BASE_URL or the Resonance endpoint.")
    parser.add_argument("--token", default=None, help="Defaults to $IQM_TOKEN.")
    parser.add_argument("--tokens-file", default=None, help="Defaults to $IQM_TOKENS_FILE.")
    args = parser.parse_args()

    log.info("Starting backend discovery example (backend=%s, min_qubits=%d)", args.backend, args.min_qubits)

    if args.backend == "sim":
        _run_simulator(args.min_qubits)
        return

    _run_discovery(
        base_url=args.base_url,
        token=args.token,
        tokens_file=args.tokens_file,
        min_qubits=args.min_qubits,
    )


if __name__ == "__main__":
    main()
