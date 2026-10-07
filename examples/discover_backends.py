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
# dependencies = [
#   "iqm-qdmi",
#   "mqt-core~=4.0.0",
#   "requests>=2.31",
# ]
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# ///

"""List IQM quantum computers and select the largest meeting a qubit constraint.

The IQM Server REST API supplies the inventory. MQT Core 4's QDMI driver
opens a fresh session for each computer by ID and exposes status, qubit count,
T1/T2, and gate fidelity. The driver registry alone cannot enumerate an IQM
server: each registered IQM device opens one quantum computer.

This example queries properties only; it does not submit quantum jobs.
"""

from __future__ import annotations

import argparse
import json
import logging
import operator
import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING, Any

import requests
from mqt.core.qdmi.driver import DeviceDefinition, open_device, register_device_if_absent

from iqm.qdmi import IQM_QDMI_DEVICE_ID, IQM_QDMI_LIBRARY_PATH, IQM_QDMI_PREFIX

if TYPE_CHECKING:
    from mqt.core.qdmi import Device

log = logging.getLogger(__name__)

_SIMULATOR_DEVICE = "mqt.ddsim.default"
_DEFAULT_BASE_URL = "https://resonance.iqm.tech"


def resolve_base_url(base_url: str | None) -> str:
    """Resolve the IQM Server endpoint.

    Returns:
        The explicit URL, environment URL, or Resonance endpoint.
    """
    return base_url or os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL") or _DEFAULT_BASE_URL


def resolve_credentials(token: str | None, tokens_file: str | None) -> tuple[str | None, str | None]:
    """Resolve the same authentication source for inventory and device queries.

    Returns:
        A token or tokens-file path, with the other source unset.

    Raises:
        ValueError: If both authentication sources are configured.
    """
    if token or tokens_file:
        resolved_token, resolved_file = token or None, tokens_file or None
    else:
        resolved_token = os.getenv("IQM_TOKEN") or None
        resolved_file = os.getenv("IQM_TOKENS_FILE") or None
    if resolved_token and resolved_file:
        msg = "Provide only one of --token/$IQM_TOKEN or --tokens-file/$IQM_TOKENS_FILE."
        raise ValueError(msg)
    return resolved_token, resolved_file


def list_quantum_computers(base_url: str, bearer_token: str | None) -> list[dict[str, Any]]:
    """Fetch the quantum computers available on an IQM Server.

    Returns:
        The raw `quantum_computers` entries reported by the server.

    Raises:
        TypeError: If the response lacks a quantum_computers array.
        ValueError: If an inventory entry lacks a valid ID.
    """
    url = base_url.rstrip("/") + "/api/v1/quantum-computers"
    headers = {"Authorization": f"Bearer {bearer_token}"} if bearer_token else {}
    response = requests.get(url, headers=headers, timeout=30)
    response.raise_for_status()
    data = response.json()
    if not isinstance(data, dict) or not isinstance(data.get("quantum_computers"), list):
        msg = "Expected a quantum_computers array in the IQM Server response."
        raise TypeError(msg)
    quantum_computers = data["quantum_computers"]
    if any(not isinstance(qc, dict) or not isinstance(qc.get("id"), str) or not qc["id"] for qc in quantum_computers):
        msg = "Expected every quantum computer to have a nonempty string ID."
        raise ValueError(msg)
    return quantum_computers


def calibration_summary(device: Device) -> str:
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


def coherence_summary(device: Device) -> str:
    """Summarize per-site T1/T2 coherence times from the device's public `Site` API, if available.

    Returns:
        A short human-readable coherence-time summary.
    """
    sites = device.regular_sites()
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


def run_simulator(min_qubits: int) -> None:
    """Select local DDSIM if it meets the constraint, without contacting IQM."""
    log.info("Simulator backend selected: skipping IQM Server discovery.")
    device = open_device(_SIMULATOR_DEVICE)
    log.info("Backend ready: '%s' | %d qubits", device.name(), device.qubits_num())

    if device.qubits_num() < min_qubits:
        sys.exit(f"Simulator exposes {device.qubits_num()} qubits, fewer than --min-qubits={min_qubits}.")

    log.info("Calibration quality: n/a (simulator devices are noiseless)")
    log.info("Selected backend: '%s'", device.name())
    log.info("Done.")


def run_discovery(*, base_url: str | None, token: str | None, tokens_file: str | None, min_qubits: int) -> None:
    """Query candidates and select the largest meeting the qubit constraint.

    Raises:
        ValueError: If credentials or the inventory are invalid.
    """
    resolved_base_url = resolve_base_url(base_url)
    log.info("Discovering quantum computers available at '%s'...", resolved_base_url)
    token, tokens_file = resolve_credentials(token, tokens_file)
    bearer_token = token
    if tokens_file:
        data = json.loads(Path(tokens_file).read_text(encoding="utf-8"))
        bearer_token = data.get("access_token") if isinstance(data, dict) else None
        if not isinstance(bearer_token, str) or not bearer_token:
            msg = "The tokens file must contain a nonempty string access_token."
            raise ValueError(msg)
    quantum_computers = list_quantum_computers(resolved_base_url, bearer_token)
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

    register_device_if_absent(DeviceDefinition(IQM_QDMI_DEVICE_ID, IQM_QDMI_LIBRARY_PATH, IQM_QDMI_PREFIX))
    candidates: list[tuple[str, int, str]] = []
    for qc in quantum_computers:
        name = qc.get("alias") or qc["id"]
        log.info("Querying properties of '%s'...", name)
        try:
            device = open_device(
                IQM_QDMI_DEVICE_ID,
                base_url=resolved_base_url,
                token=token,
                auth_file=tokens_file,
                custom1=qc["id"],
            )
            qubits = device.qubits_num()
            status = device.status().name
            quality = calibration_summary(device)
            coherence = coherence_summary(device)
        except (RuntimeError, ValueError):
            log.warning("Skipping '%s': failed to query device properties.", name)
            continue
        log.info("  '%s': status=%s, %d qubits, %s, %s", name, status, qubits, quality, coherence)
        candidates.append((name, qubits, quality))

    matching = [candidate for candidate in candidates if candidate[1] >= min_qubits]
    if not matching:
        largest = max((candidate[1] for candidate in candidates), default=0)
        sys.exit(f"No discovered quantum computer meets --min-qubits={min_qubits} (largest available: {largest}).")

    selected_alias, selected_qubits, selected_quality = max(matching, key=operator.itemgetter(1))
    log.info(
        "Selected '%s': %d qubits (>= --min-qubits=%d), %s",
        selected_alias,
        selected_qubits,
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
    parser.add_argument("--base-url", default=None, help="Defaults to $IQM_SERVER_URL, $IQM_BASE_URL, or Resonance.")
    parser.add_argument("--token", default=None, help="Defaults to $IQM_TOKEN.")
    parser.add_argument("--tokens-file", default=None, help="Defaults to $IQM_TOKENS_FILE.")
    args = parser.parse_args()
    if args.min_qubits < 1:
        parser.error("--min-qubits must be positive.")

    log.info("Starting backend discovery example (backend=%s, min_qubits=%d)", args.backend, args.min_qubits)

    if args.backend == "sim":
        run_simulator(args.min_qubits)
        return

    try:
        run_discovery(
            base_url=args.base_url,
            token=args.token,
            tokens_file=args.tokens_file,
            min_qubits=args.min_qubits,
        )
    except (requests.RequestException, OSError, TypeError, ValueError) as exc:
        sys.exit(f"Discovery failed ({type(exc).__name__}). Check the server URL and authentication settings.")


if __name__ == "__main__":
    main()
