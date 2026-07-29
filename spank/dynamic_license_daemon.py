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

"""Prototype daemon that mirrors IQM QC availability into a Slurm Dynamic License.

**This is an RFC-quality prototype, not production-ready tooling.** See
`docs/spank_plugin.md` (section "Reflecting Live QC Availability with Dynamic
Licenses") for the design rationale, the honest limitations of each
availability signal below, and the open questions this prototype
intentionally leaves for reviewers.

In short: the SPANK plugin's static `Licenses=` mechanism (see "Limiting
Concurrent Access with Slurm Licenses" in the same document) caps how many
jobs Slurm lets through, but that cap is a fixed number, not a live read of
whether the quantum computer (QC) is actually free right now. Slurm's Dynamic
License mechanism (Slurm 23.02+) allows an external process to update a
license's available count live via

    sacctmgr -i modify resource <name> set lastconsumed=<0|1>

This script polls one or more configurable **signal sources** for a given QC
and drives that command accordingly, so Slurm can hold jobs back until the QC
looks available. Three sources are available, and more than one can be
combined (a QC is reported unavailable if *any* enabled source says so):

- `lock` (**default, and the recommended primary source**): reads the
  file-based lease lock in `license_lock.py`, which models "does a
  Slurm-mediated job currently hold this QC" directly. It requires wiring a
  Slurm `Prolog`/`Epilog` (or `TaskProlog`/`TaskEpilog`) script to call
  `license_lock.py acquire`/`release` -- see `docs/spank_plugin.md`. Until
  that is wired up for a given cluster, the lock is simply never held and
  this source always reports "available".
- `qdmi-status` (legacy, off by default): polls
  `QDMI_DEVICE_PROPERTY_STATUS`. This is a **session-local** signal -- see
  `is_available()`'s docstring -- and does not reflect other tenants' usage
  of the QC. Kept for backwards compatibility and as an optional
  supplementary check, not recommended as the sole source.
- `queue-length` (**experimental, off by default, cloud/pay-as-you-go QCs
  only**): best-effort polling of an IQM Server API queue-status endpoint
  that reports a real, cross-tenant `queue_length`. The exact endpoint path
  is *not confirmed* and is deliberately not hardcoded here -- see
  `QueueLengthSignal` and `docs/spank_plugin.md`. Its applicability to
  on-premise deployments is unverified; do not enable it there.

Usage:

    python3 dynamic_license_daemon.py --qc-alias emerald

Run with `--help` for the full list of options; every option also has an
`IQM_DYNAMIC_LICENSE_*` (or, for credentials/QC selection, plain `IQM_*`)
environment variable equivalent.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, ClassVar, Protocol

import license_lock

if TYPE_CHECKING:
    from mqt.core.fomac import Device

LOGGER = logging.getLogger("iqm_dynamic_license_daemon")

#: `Device.Status` values that should be reflected as "unavailable" (Slurm
#: license consumed, i.e. `lastconsumed=1`). Everything else (currently only
#: `IDLE`) is reflected as "available" (`lastconsumed=0`).
_UNAVAILABLE_STATUS_NAMES = frozenset({"OFFLINE", "BUSY", "ERROR", "MAINTENANCE", "CALIBRATION"})

#: Recognized `--signal-source`/`IQM_DYNAMIC_LICENSE_SIGNAL_SOURCES` values.
KNOWN_SIGNAL_SOURCES = ("lock", "qdmi-status", "queue-length")


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
    signal_sources: tuple[str, ...]
    lock_dir: Path
    queue_status_url_template: str | None
    http_timeout_seconds: float


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

    Exits the process (via `argparse`) if required arguments are missing or
    an unrecognized `--signal-source` value is given.

    Returns:
        The resolved `DaemonConfig` for this run.
    """
    parser = argparse.ArgumentParser(
        prog="dynamic_license_daemon",
        description=(
            "Prototype daemon mirroring IQM QC availability into a Slurm Dynamic License "
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
        "--signal-source",
        dest="signal_sources",
        action="append",
        choices=KNOWN_SIGNAL_SOURCES,
        default=None,
        help=(
            "Availability signal to consult; may be given multiple times to combine sources "
            "(unavailable if any reports so). Defaults to `IQM_DYNAMIC_LICENSE_SIGNAL_SOURCES` "
            "(comma-separated), then to just `lock`. See the module docstring for what each source means."
        ),
    )
    parser.add_argument(
        "--lock-dir",
        default=_env_default("IQM_DYNAMIC_LICENSE_LOCK_DIR", "/var/run/iqm-dynamic-license"),
        help="Lease lock directory used by the `lock` source (`IQM_DYNAMIC_LICENSE_LOCK_DIR`); "
        "must match what Prolog/Epilog scripts pass to license_lock.py.",
    )
    parser.add_argument(
        "--queue-status-url-template",
        default=_env_default("IQM_DYNAMIC_LICENSE_QUEUE_STATUS_URL_TEMPLATE"),
        help=(
            "EXPERIMENTAL, required by the `queue-length` source: a URL template with a `{qc_id}` "
            "placeholder for the (unconfirmed) IQM Server API queue-status endpoint. Left unset by "
            "design -- see docs/spank_plugin.md before setting this "
            "(`IQM_DYNAMIC_LICENSE_QUEUE_STATUS_URL_TEMPLATE`)."
        ),
    )
    parser.add_argument(
        "--poll-interval",
        type=float,
        default=float(_env_default("IQM_DYNAMIC_LICENSE_POLL_INTERVAL_SECONDS", "30") or 30),
        help="Seconds between polls (default: 30, or `IQM_DYNAMIC_LICENSE_POLL_INTERVAL_SECONDS`).",
    )
    parser.add_argument(
        "--http-timeout",
        type=float,
        default=float(_env_default("IQM_DYNAMIC_LICENSE_HTTP_TIMEOUT_SECONDS", "10") or 10),
        help="Timeout in seconds for the `queue-length` source's HTTP request (default: 10).",
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

    if args.signal_sources:
        signal_sources = tuple(args.signal_sources)
    else:
        env_sources = _env_default("IQM_DYNAMIC_LICENSE_SIGNAL_SOURCES")
        signal_sources = tuple(s.strip() for s in env_sources.split(",") if s.strip()) if env_sources else ("lock",)
    unknown = [s for s in signal_sources if s not in KNOWN_SIGNAL_SOURCES]
    if unknown:
        known = ", ".join(KNOWN_SIGNAL_SOURCES)
        parser.error(f"unknown --signal-source value(s): {', '.join(unknown)} (known: {known})")
    if "queue-length" in signal_sources and not args.queue_status_url_template:
        parser.error("--signal-source queue-length requires --queue-status-url-template (see docs/spank_plugin.md)")

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
        signal_sources=signal_sources,
        lock_dir=Path(args.lock_dir),
        queue_status_url_template=args.queue_status_url_template,
        http_timeout_seconds=args.http_timeout,
    )


class AvailabilitySignal(Protocol):
    """A single, independently pollable availability signal."""

    name: ClassVar[str]

    def probe(self) -> bool | None:
        """Return `True`/`False` if this source has an opinion, `None` if unknown right now."""
        ...


@dataclass
class LockSignal:
    """Primary signal: whether a Slurm-mediated job holds the file-based lease lock.

    See `license_lock.py` for the acquire/release semantics and how it is
    meant to be wired via Slurm `Prolog`/`Epilog` scripts. Until that wiring
    exists on a given cluster, the lock is never held and this always
    reports available (`True`) -- it does not silently guess.
    """

    name: ClassVar[str] = "lock"
    lock_dir: Path
    resource_name: str

    def probe(self) -> bool | None:
        """Return `True` if unlocked, `False` if a live lease is held.

        Returns:
            `True` when no live lease is held for `resource_name`, `False`
            otherwise. This source never returns `None`: an unreadable lease
            file is already treated as "not held" by `license_lock.is_held`.
        """
        held = license_lock.is_held(self.lock_dir, self.resource_name)
        LOGGER.debug("lock signal: held=%s", held)
        return not held


@dataclass
class QdmiStatusSignal:
    """Legacy/secondary signal: QDMI's `QDMI_DEVICE_PROPERTY_STATUS`.

    Kept open for the daemon's lifetime: `Device.status()` is a cheap, local
    property read, so there is no need to reopen a session on every poll.
    """

    name: ClassVar[str] = "qdmi-status"
    device: Device

    def probe(self) -> bool | None:
        """Return whether the device looks available, per `QDMI_DEVICE_PROPERTY_STATUS`.

        Returns:
            `True` if the device reports `IDLE`, `False` for any other
            status. **This is a session-local signal**: it reflects only
            whether *this daemon's own* session has an outstanding job (it
            never submits one), not whether other tenants are using the QC.
            See the module docstring and `docs/spank_plugin.md`.
        """
        status = self.device.status()
        unavailable = status.name in _UNAVAILABLE_STATUS_NAMES
        LOGGER.debug("qdmi-status signal: %s (available=%s)", status.name, not unavailable)
        return not unavailable


@dataclass
class QueueLengthSignal:
    """EXPERIMENTAL signal: a real, cross-tenant queue-depth count -- if it exists for this QC.

    The IQM Server API exposes a "get quantum computer queue status"
    endpoint returning a body of the shape
    `{"available": [...], "queue_length": <int>}` for pay-as-you-go/cloud
    QCs, per the API reference. Its exact path is not confirmed (the
    reference text available while writing this does not show raw paths),
    and its applicability to on-premise/Station Control deployments is
    unverified -- Resonance is known to manage its own queue this way, but
    that does not necessarily hold for on-prem devices. Hence this class
    requires the caller to supply the URL template explicitly (see
    `--queue-status-url-template`) rather than guessing a path, and treats
    any request failure (network error, 404, unexpected schema, ...) as
    "unknown" rather than "busy", so a wrong/missing endpoint degrades to a
    no-op instead of wrongly blocking every job.
    """

    name: ClassVar[str] = "queue-length"
    url_template: str
    qc_id: str
    token: str | None
    timeout_seconds: float
    _warned: bool = False

    def probe(self) -> bool | None:
        """Return whether the reported queue is empty, or `None` if the probe was inconclusive.

        Returns:
            `True` if `queue_length == 0`, `False` if `queue_length > 0`,
            `None` if the request failed or the response did not match the
            expected shape (logged once at WARNING, then DEBUG, to avoid
            repeated noise every poll interval).
        """
        url = self.url_template.format(qc_id=self.qc_id)
        request = urllib.request.Request(url, headers={"Authorization": f"Bearer {self.token}"} if self.token else {})  # ruff: ignore[suspicious-url-open-usage]
        try:
            with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:  # ruff: ignore[suspicious-url-open-usage]
                payload = json.loads(response.read())
            queue_length = int(payload["queue_length"])
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, TypeError, ValueError) as e:
            log = LOGGER.warning if not self._warned else LOGGER.debug
            log("queue-length signal inconclusive (%s: %s); ignoring this source for now", type(e).__name__, e)
            self._warned = True
            return None
        LOGGER.debug("queue-length signal: queue_length=%d", queue_length)
        return queue_length == 0


def build_signals(config: DaemonConfig) -> list[AvailabilitySignal]:
    """Instantiate the `AvailabilitySignal`s selected by `config.signal_sources`.

    Returns:
        One `AvailabilitySignal` per entry in `config.signal_sources`, in
        that order.
    """
    signals: list[AvailabilitySignal] = []
    for source in config.signal_sources:
        if source == "lock":
            signals.append(LockSignal(lock_dir=config.lock_dir, resource_name=config.resource_name))
        elif source == "qdmi-status":
            signals.append(QdmiStatusSignal(device=load_device(config)))
        elif source == "queue-length":
            assert config.queue_status_url_template is not None
            signals.append(
                QueueLengthSignal(
                    url_template=config.queue_status_url_template,
                    qc_id=config.qc_id or config.qc_alias or "",
                    token=config.token,
                    timeout_seconds=config.http_timeout_seconds,
                )
            )
    return signals


def load_device(config: DaemonConfig) -> Device:
    """Load the packaged IQM QDMI device library and open a session for the target QC.

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


def _probe_signal(signal: AvailabilitySignal) -> bool | None:
    """Probe a single `signal`, converting any exception into a fail-closed `False`.

    Returns:
        The signal's own `probe()` result, or `False` if it raised.
    """
    try:
        return signal.probe()
    except Exception:
        LOGGER.exception("Signal %s raised; treating as unavailable (fail-closed)", signal.name)
        return False


def is_available(signals: list[AvailabilitySignal]) -> bool:
    """Combine all `signals` into a single availability verdict.

    The QC is reported unavailable if *any* signal explicitly says so
    (conservative, fail-closed composition). If every signal is inconclusive
    (`None`) -- including the degenerate case of no signals at all -- the QC
    is also reported unavailable, since there is then no positive evidence
    that it is free.

    Returns:
        `True` only if at least one signal reported `True` and none reported
        `False`.
    """
    results = {signal.name: _probe_signal(signal) for signal in signals}

    LOGGER.debug("Signal results: %s", results)
    if any(result is False for result in results.values()):
        return False
    if any(result is True for result in results.values()):
        return True
    LOGGER.warning("No signal produced a definite result; treating QC as unavailable (fail-closed)")
    return False


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
        "Starting dynamic license daemon: resource=%s qc_id=%s qc_alias=%s signal_sources=%s "
        "poll_interval=%ss dry_run=%s",
        config.resource_name,
        config.qc_id,
        config.qc_alias,
        ",".join(config.signal_sources),
        config.poll_interval_seconds,
        config.dry_run,
    )
    signals = build_signals(config)

    last_applied: bool | None = None
    while True:
        available = is_available(signals)

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
