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
quantum computer known to an IQM Server: each opened QDMI device session
resolves to exactly *one* quantum computer, selected by ID, alias, or "first
available" during session initialization (`IQM_QDMI_device_session_init` /
`Process_static_quantum_architecture` in `src/iqm_device.cpp`; see
docs/usage.md). This example fills that gap for discovery *scripts* rather
than the library: it issues the same `api/v1/quantum-computers` request the
device session already performs internally to learn the available aliases,
then opens each candidate's device and queries its public properties (qubit
count, status, per-site T1/T2, and per-operation fidelity) through the public
`mqt.core.fomac.Device`/`Site`/`Operation` API to pick one that satisfies a
`--min-qubits` constraint.

Future direction:

- True multi-QC enumeration without the `api/v1/quantum-computers` REST call
  above is not something any current or planned `mqt-core` release can fix on
  its own, because the root cause lives in *this repo's* C++ device
  implementation, not in `mqt-core`: `mqt.core.fomac.Session.get_devices()`
  only ever returns one `Device` per registered `DeviceDefinition`, and (per
  the session-initialization behavior above) each `DeviceDefinition` this
  library can hand `mqt-core` still resolves to exactly one IQM quantum
  computer. Registering one `DeviceDefinition` per alias would still require
  knowing every alias up front - the same requirement the REST call exists to
  satisfy. An open (unmerged) `mqt-core` pull request,
  https://github.com/munich-quantum-toolkit/core/pull/1912 ("Add configurable
  QDMI device discovery"), adds exactly that `DeviceRegistry`/`DeviceDefinition`
  mechanism (`qdmi.json` / `[tool.qdmi]` / env-var configuration), which would
  be the right way to *register* multiple already-known aliases as separate
  `Device`s - but it is relevant only as context here, not as a fix for the
  enumeration problem itself, which needs a change to this repo's own C++
  session initialization to resolve. (A related, larger, also-open PR,
  https://github.com/munich-quantum-toolkit/core/pull/1901, redesigns FoMaC
  and `qdmi::Driver` around that same registry and was reportedly exercised
  against IQM's own QDMI implementation branches during development.) Neither
  PR has merged, and this note describes context, not a plan this repo
  currently depends on.
- The IQM Server API is known to expose a queue-length /
  execution-availability-window signal, described for the "pay-as-you-go
  queue" and therefore apparently cloud/Resonance-oriented (its availability
  and semantics for on-premise quantum computers are unconfirmed). That
  signal is not currently surfaced through QDMI-on-IQM's Python or C++
  bindings, so it is not used here. Once it is exposed through the library, a
  natural enhancement to this example would be ranking candidate backends by
  queue depth in addition to qubit count and fidelity. This is an open
  question independent of the `mqt-core` discussion above - it concerns IQM
  Server API queue depth, not `mqt-core`'s device object model.
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
from mqt.core.fomac import Device, add_dynamic_device_library
from mqt.core.plugins.qiskit.provider import QDMIProvider

from iqm.qdmi import IQM_QDMI_LIBRARY_PATH

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


def _open_device(base_url: str, token: str | None, tokens_file: str | None, qc_alias: str) -> Device:
    """Open a public FoMaC `Device` for one IQM quantum computer.

    This calls the same `mqt.core.fomac.add_dynamic_device_library` entry point
    `IQMBackend` uses internally, so the returned `Device` exposes qubit count,
    status, per-site T1/T2, and per-operation fidelity through the public API,
    without reaching into any private `QDMIBackend` attribute.

    Returns:
        The opened FoMaC `Device` for `qc_alias`.
    """
    return add_dynamic_device_library(
        library_path=str(IQM_QDMI_LIBRARY_PATH),
        prefix="IQM",
        base_url=base_url,
        token=token,
        auth_file=tokens_file,
        custom2=qc_alias,
    )


def _calibration_summary(device: Device) -> str:
    """Summarize two-qubit gate fidelity from the device's public `Operation` API, if available.

    Returns:
        A short human-readable calibration-quality summary.
    """
    for op in device.operations():
        site_pairs = op.site_pairs()
        if not site_pairs:
            continue
        fidelities = [fidelity for pair in site_pairs if (fidelity := op.fidelity(sites=pair)) is not None]
        if fidelities:
            mean_fidelity = sum(fidelities) / len(fidelities)
            return f"'{op.name()}' mean 2-qubit fidelity {mean_fidelity:.4f} over {len(fidelities)} pair(s)"
    return "no two-qubit gate fidelity exposed by the device"


def _coherence_summary(device: Device) -> str:
    """Summarize per-site T1/T2 coherence times from the device's public `Site` API, if available.

    Returns:
        A short human-readable coherence-time summary.
    """
    sites = [site for site in device.sites() if not site.is_zone()]
    t1_values = [t1 for site in sites if (t1 := site.t1()) is not None]
    t2_values = [t2 for site in sites if (t2 := site.t2()) is not None]
    if not t1_values and not t2_values:
        return "no T1/T2 data exposed by the device"
    unit = device.duration_unit() or "device time units"
    parts = []
    if t1_values:
        parts.append(f"mean T1 {sum(t1_values) / len(t1_values):.1f} {unit}")
    if t2_values:
        parts.append(f"mean T2 {sum(t2_values) / len(t2_values):.1f} {unit}")
    return ", ".join(parts)


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

    candidates: list[tuple[str, Device, str]] = []
    for qc in quantum_computers:
        alias = qc.get("alias")
        if not alias:
            log.warning("Skipping quantum computer without an alias: %s", qc)
            continue
        log.info("Querying properties of '%s'...", alias)
        try:
            device = _open_device(resolved_base_url, token, tokens_file, alias)
        except Exception as exc:  # ruff:ignore[blind-except] - keep discovering the remaining candidates
            log.warning("Skipping '%s': failed to query device properties (%s)", alias, exc)
            continue
        status = device.status().name
        quality = _calibration_summary(device)
        coherence = _coherence_summary(device)
        log.info(
            "  '%s': status=%s, %d qubits, %s, %s",
            alias,
            status,
            device.qubits_num(),
            quality,
            coherence,
        )
        candidates.append((alias, device, quality))

    matching = [candidate for candidate in candidates if candidate[1].qubits_num() >= min_qubits]
    if not matching:
        largest = max((candidate[1].qubits_num() for candidate in candidates), default=0)
        sys.exit(f"No discovered quantum computer meets --min-qubits={min_qubits} (largest available: {largest}).")

    selected_alias, selected_device, selected_quality = max(matching, key=lambda candidate: candidate[1].qubits_num())
    log.info(
        "Selected '%s': %d qubits (>= --min-qubits=%d), %s",
        selected_alias,
        selected_device.qubits_num(),
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
