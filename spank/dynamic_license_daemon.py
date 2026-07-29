#!/usr/bin/env python3
# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

"""Prototype daemon that mirrors an IQM QC's device status into a Slurm Dynamic License.

**This is an RFC-quality prototype, not production-ready tooling.** See
`docs/spank_plugin.md` (section "Reflecting Live QC Availability with Dynamic
Licenses") for the design rationale, the honest limitations of the "QC status"
signal used here, and the open questions this prototype intentionally leaves
for reviewers.

In short: the SPANK plugin's static `Licenses=` mechanism (see "Limiting
Concurrent Access with Slurm Licenses" in the same document) caps how many
jobs Slurm lets through, but that cap is a fixed number, not a live read of
whether the quantum computer (QC) is actually free right now. Slurm's Dynamic
License mechanism (Slurm 23.02+) allows an external process to update a
license's available count live via

    sacctmgr -i modify resource <name> set lastconsumed=<0|1>

This script polls `QDMI_DEVICE_PROPERTY_STATUS` for a given QC (via the
`mqt.core.fomac` bindings that back this package's Qiskit integration) and
drives that command accordingly, so Slurm can hold jobs back until the QC
reports itself idle.

Usage:

    python3 dynamic_license_daemon.py --qc-alias emerald

Run with `--help` for the full list of options; every option also has an
`IQM_DYNAMIC_LICENSE_*` (or, for credentials/QC selection, plain `IQM_*`)
environment variable equivalent.
"""

from __future__ import annotations

import argparse
import logging
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from mqt.core.fomac import Device

LOGGER = logging.getLogger("iqm_dynamic_license_daemon")

#: `Device.Status` values that should be reflected as "unavailable" (Slurm
#: license consumed, i.e. `lastconsumed=1`). Everything else (currently only
#: `IDLE`) is reflected as "available" (`lastconsumed=0`).
_UNAVAILABLE_STATUS_NAMES = frozenset({"OFFLINE", "BUSY", "ERROR", "MAINTENANCE", "CALIBRATION"})


@dataclass(frozen=True)
class DaemonConfig:
    """Resolved configuration for a single daemon run."""

    resource_name: str
    poll_interval_seconds: float
    cluster: str | None
    dry_run: bool
    once: bool
    base_url: str | None
    token: str | None
    tokens_file: str | None
    qc_id: str | None
    qc_alias: str | None


def _env_default(name: str, fallback: str | None = None) -> str | None:
    """Return a non-empty environment variable value, or `fallback`.

    Returns:
        The environment variable's value, or `fallback` if unset/empty.
    """
    value = os.getenv(name)
    return value or fallback


def _env_truthy(name: str, *, default: bool = False) -> bool:
    """Interpret an environment variable as a boolean flag.

    Returns:
        `True` if the variable is set to a recognized truthy value, `False`
        if set to a recognized falsy value, and `default` if unset or
        unrecognized.
    """
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on", "enabled"}


def parse_args(argv: list[str] | None = None) -> DaemonConfig:
    """Parse command-line arguments, falling back to `IQM_*` environment variables.

    Returns:
        The resolved `DaemonConfig` for this run.
    """
    parser = argparse.ArgumentParser(
        prog="dynamic_license_daemon",
        description=(
            "Prototype daemon mirroring an IQM QC's QDMI device status into a Slurm Dynamic License "
            "via `sacctmgr -i modify resource <name> set lastconsumed=<0|1>`. RFC-quality; see "
            "docs/spank_plugin.md for the caveats before relying on this."
        ),
    )
    parser.add_argument(
        "--resource-name",
        default=_env_default("IQM_DYNAMIC_LICENSE_RESOURCE_NAME"),
        help=(
            "Name of the Slurm Dynamic License resource to update (as created via `sacctmgr add resource "
            "name=... type=license`). Defaults to `IQM_DYNAMIC_LICENSE_RESOURCE_NAME`, then to "
            "`iqm_qc_<qc-alias>` (matching the SPANK plugin's default `iqm_license_prefix`)."
        ),
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=float(_env_default("IQM_DYNAMIC_LICENSE_POLL_INTERVAL_SECONDS", "30") or 30),
        help="Seconds between polls (default: 30, or `IQM_DYNAMIC_LICENSE_POLL_INTERVAL_SECONDS`).",
    )
    parser.add_argument(
        "--cluster",
        default=_env_default("IQM_DYNAMIC_LICENSE_CLUSTER"),
        help="Optional Slurm cluster name to scope the `sacctmgr` update to (`IQM_DYNAMIC_LICENSE_CLUSTER`).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        default=_env_truthy("IQM_DYNAMIC_LICENSE_DRY_RUN"),
        help="Log the `sacctmgr` command instead of executing it (`IQM_DYNAMIC_LICENSE_DRY_RUN`).",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Poll a single time and exit, instead of looping forever. Useful for testing or cron-style setups.",
    )
    parser.add_argument(
        "--base-url",
        default=_env_default("IQM_BASE_URL"),
        help="IQM Server base URL (defaults to `IQM_BASE_URL`, then the packaged default).",
    )
    parser.add_argument("--token", default=_env_default("IQM_TOKEN"), help="IQM auth token (`IQM_TOKEN`).")
    parser.add_argument(
        "--tokens-file",
        default=_env_default("IQM_TOKENS_FILE"),
        help="Path to an IQM tokens file (`IQM_TOKENS_FILE`).",
    )
    parser.add_argument("--qc-id", default=_env_default("IQM_QC_ID"), help="Target QC ID (`IQM_QC_ID`).")
    parser.add_argument("--qc-alias", default=_env_default("IQM_QC_ALIAS"), help="Target QC alias (`IQM_QC_ALIAS`).")
    parser.add_argument(
        "--log-level",
        default=_env_default("IQM_DYNAMIC_LICENSE_LOG_LEVEL", "INFO"),
        help="Python logging level name (default: INFO).",
    )

    args = parser.parse_args(argv)

    if not args.qc_id and not args.qc_alias:
        parser.error("one of --qc-id/IQM_QC_ID or --qc-alias/IQM_QC_ALIAS is required")

    default_resource_name = f"iqm_qc_{args.qc_alias}" if args.qc_alias else None
    resource_name = args.resource_name or default_resource_name
    if not resource_name:
        parser.error("--resource-name/IQM_DYNAMIC_LICENSE_RESOURCE_NAME is required when --qc-alias is not set")

    logging.basicConfig(
        level=args.log_level.upper(),
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )

    return DaemonConfig(
        resource_name=resource_name,
        poll_interval_seconds=args.poll_interval,
        cluster=args.cluster,
        dry_run=args.dry_run,
        once=args.once,
        base_url=args.base_url,
        token=args.token,
        tokens_file=args.tokens_file,
        qc_id=args.qc_id,
        qc_alias=args.qc_alias,
    )


def load_device(config: DaemonConfig) -> Device:
    """Load the packaged IQM QDMI device library and open a session for the target QC.

    Kept open for the daemon's lifetime: `Device.status()` is a cheap, local
    property read (see the module docstring and `docs/spank_plugin.md` for why
    that matters), so there is no need to reopen a session on every poll.

    Returns:
        The initialized `mqt.core.fomac.Device` handle for the target QC.

    Raises:
        ImportError: If `mqt-core` (the `iqm-qdmi[qiskit]` extra) is not installed.
    """
    try:
        from mqt.core.fomac import add_dynamic_device_library  # ruff: ignore[import-outside-top-level]
    except ImportError as e:
        msg = (
            "This daemon requires `mqt-core`, e.g. via `uv pip install iqm-qdmi[qiskit]`. "
            "See docs/spank_plugin.md for details."
        )
        raise ImportError(msg) from e

    from iqm.qdmi import IQM_QDMI_LIBRARY_PATH  # ruff: ignore[import-outside-top-level]

    return add_dynamic_device_library(
        library_path=str(IQM_QDMI_LIBRARY_PATH),
        prefix="IQM",
        base_url=config.base_url,
        token=config.token,
        auth_file=config.tokens_file,
        custom1=config.qc_id,
        custom2=config.qc_alias,
    )


def is_available(device: Device) -> bool:
    """Return whether `device` currently looks available, per `QDMI_DEVICE_PROPERTY_STATUS`.

    Returns:
        `True` if the device reports `IDLE`, `False` for any other status
        (including statuses this prototype cannot reach in practice today --
        see the module docstring's honesty caveat).
    """
    status = device.status()
    unavailable = status.name in _UNAVAILABLE_STATUS_NAMES
    LOGGER.debug("QDMI device status: %s (available=%s)", status.name, not unavailable)
    return not unavailable


def apply_lastconsumed(config: DaemonConfig, *, available: bool) -> None:
    """Run (or log, if `--dry-run`) the `sacctmgr` command reflecting `available`."""
    lastconsumed = 0 if available else 1
    cmd = ["sacctmgr", "-i", "modify", "resource", config.resource_name]
    if config.cluster:
        cmd.append(f"cluster={config.cluster}")
    cmd += ["set", f"lastconsumed={lastconsumed}"]

    if config.dry_run:
        LOGGER.info("[dry-run] would run: %s", " ".join(cmd))
        return

    LOGGER.info("Running: %s", " ".join(cmd))
    result = subprocess.run(cmd, check=False, capture_output=True, text=True, timeout=30)  # ruff: ignore[subprocess-without-shell-equals-true]
    if result.returncode != 0:
        LOGGER.error("sacctmgr failed (rc=%s): %s", result.returncode, result.stderr.strip())
    elif result.stdout.strip():
        LOGGER.debug("sacctmgr output: %s", result.stdout.strip())


def run(config: DaemonConfig) -> None:
    """Run the poll loop (or a single poll, if `config.once`)."""
    LOGGER.info(
        "Starting dynamic license daemon: resource=%s qc_id=%s qc_alias=%s poll_interval=%ss dry_run=%s",
        config.resource_name,
        config.qc_id,
        config.qc_alias,
        config.poll_interval_seconds,
        config.dry_run,
    )
    device = load_device(config)

    last_applied: bool | None = None
    while True:
        try:
            available = is_available(device)
        except Exception:
            LOGGER.exception("Failed to query device status; treating QC as unavailable (fail-closed)")
            available = False

        if available != last_applied:
            apply_lastconsumed(config, available=available)
            last_applied = available
        else:
            LOGGER.debug("No status change (available=%s); skipping sacctmgr call", available)

        if config.once:
            return
        time.sleep(config.poll_interval_seconds)


def main(argv: list[str] | None = None) -> None:
    """Entry point for running this module as a script."""
    config = parse_args(argv)
    try:
        run(config)
    except KeyboardInterrupt:
        LOGGER.info("Interrupted; exiting")
        sys.exit(0)


if __name__ == "__main__":
    main()
