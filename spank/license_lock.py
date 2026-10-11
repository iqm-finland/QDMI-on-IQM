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

"""File-based lease lock tracking whether a Slurm-mediated job holds an IQM QC.

**This is an RFC-quality prototype**, see `docs/spank_plugin.md` ("Reflecting
Live QC Availability with Dynamic Licenses") for the design rationale.

This module is deliberately **not** part of the compiled SPANK plugin and
requires no changes to `iqm_spank_plugin.cpp`. It is meant to be driven by
Slurm's native `Prolog`/`Epilog` (or `TaskProlog`/`TaskEpilog`) script hooks,
configured in `slurm.conf` -- a mechanism entirely orthogonal to the SPANK
plugin. A job's prolog acquires the lock for its target QC; its epilog
releases it. `dynamic_license_daemon.py` then reads this lock state as its
primary "is this QC currently claimed by a Slurm job" signal, rather than
inferring availability from a QC-side status field of uncertain accuracy (see
the module docstring of `dynamic_license_daemon.py`).

Each lease carries a TTL so that a crashed job (whose epilog never runs)
cannot leak the lock forever; a stale lease is treated as released.

This intentionally supports only a single holder per resource, matching the
common case of a static `Licenses=<resource>:1` grant per job. Pool sizes
greater than 1 (e.g. `Licenses=<resource>:4`) are out of scope for this
prototype; see `docs/spank_plugin.md` for the corresponding open question.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

LOGGER = logging.getLogger("iqm_license_lock")

#: Default lease TTL. Chosen to comfortably exceed typical job step
#: durations while still bounding how long a crashed job's epilog-less exit
#: can leak the lock; administrators should tune this to their workloads.
DEFAULT_TTL_SECONDS = 6 * 60 * 60


@dataclass(frozen=True)
class Lease:
    """A recorded lock lease."""

    holder_id: str
    expires_at: float


def _lock_path(lock_dir: Path, resource_name: str) -> Path:
    """Return the lease file path for `resource_name` under `lock_dir`.

    Returns:
        The path of the lease file.
    """
    return lock_dir / f"{resource_name}.lease.json"


def _read_lease(path: Path) -> Lease | None:
    """Return the lease recorded at `path`, or `None` if absent/unreadable.

    Returns:
        The parsed `Lease`, or `None` if the file does not exist or cannot be
        parsed (treated as "no lease held").
    """
    try:
        raw = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    try:
        data = json.loads(raw)
        return Lease(holder_id=str(data["holder_id"]), expires_at=float(data["expires_at"]))
    except (json.JSONDecodeError, KeyError, TypeError, ValueError):
        LOGGER.warning("Ignoring unreadable lease file %s", path)
        return None


def _write_lease(path: Path, lease: Lease) -> None:
    """Atomically write `lease` to `path`."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_suffix(f"{path.suffix}.tmp.{os.getpid()}")
    tmp_path.write_text(
        json.dumps({"holder_id": lease.holder_id, "expires_at": lease.expires_at}),
        encoding="utf-8",
    )
    tmp_path.replace(path)


def is_held(lock_dir: Path, resource_name: str, *, now: float | None = None) -> bool:
    """Return whether `resource_name` currently has a live (non-expired) lease.

    Returns:
        `True` if a non-expired lease exists, `False` otherwise (including
        when the lock directory or lease file does not exist).
    """
    lease = _read_lease(_lock_path(lock_dir, resource_name))
    if lease is None:
        return False
    return lease.expires_at > (now if now is not None else time.time())


def acquire(
    lock_dir: Path,
    resource_name: str,
    holder_id: str,
    *,
    ttl_seconds: float = DEFAULT_TTL_SECONDS,
    now: float | None = None,
) -> bool:
    """Acquire (or renew) the lease for `resource_name` on behalf of `holder_id`.

    Fails only if a *different*, still-live holder already has the lease
    (single-holder-per-resource semantics; see the module docstring). Safe to
    call repeatedly by the same holder to renew/extend the lease.

    Returns:
        `True` if the lease is now held by `holder_id`, `False` if a
        different holder's live lease blocked acquisition.
    """
    effective_now = now if now is not None else time.time()
    path = _lock_path(lock_dir, resource_name)
    existing = _read_lease(path)
    if existing is not None and existing.expires_at > effective_now and existing.holder_id != holder_id:
        LOGGER.info(
            "Resource %s already held by %s (expires in %.0fs); not granting to %s",
            resource_name,
            existing.holder_id,
            existing.expires_at - effective_now,
            holder_id,
        )
        return False
    _write_lease(path, Lease(holder_id=holder_id, expires_at=effective_now + ttl_seconds))
    LOGGER.info("Resource %s acquired by %s for %.0fs", resource_name, holder_id, ttl_seconds)
    return True


def release(lock_dir: Path, resource_name: str, holder_id: str) -> bool:
    """Release the lease for `resource_name` if currently held by `holder_id`.

    A release requested by a holder that does not match the recorded lease
    (e.g. a duplicate/late epilog run after the lease already expired and was
    reacquired by another job) is a silent no-op, to avoid one job releasing
    another job's active lease.

    Returns:
        `True` if a matching lease was removed, `False` otherwise.
    """
    path = _lock_path(lock_dir, resource_name)
    existing = _read_lease(path)
    if existing is None:
        return False
    if existing.holder_id != holder_id:
        LOGGER.info(
            "Not releasing resource %s: held by %s, release requested by %s",
            resource_name,
            existing.holder_id,
            holder_id,
        )
        return False
    path.unlink(missing_ok=True)
    LOGGER.info("Resource %s released by %s", resource_name, holder_id)
    return True


def _default_holder_id() -> str:
    """Return a default holder identifier derived from the Slurm job environment.

    Returns:
        `SLURM_JOB_ID` (optionally with `SLURM_STEP_ID`) if set, otherwise
        the current process ID as a last resort for manual/testing use.
    """
    job_id = os.getenv("SLURM_JOB_ID")
    if job_id:
        step_id = os.getenv("SLURM_STEP_ID")
        return f"{job_id}.{step_id}" if step_id else job_id
    return f"pid:{os.getpid()}"


def main(argv: list[str] | None = None) -> None:
    """Entry point for running this module as a script (`acquire`/`release`/`status`)."""
    parser = argparse.ArgumentParser(
        prog="license_lock",
        description=(
            "Manage the file-based lease lock used by dynamic_license_daemon.py to track "
            "whether a Slurm job currently holds an IQM QC. Intended to be called from Slurm "
            "Prolog/Epilog scripts."
        ),
    )
    parser.add_argument(
        "--lock-dir",
        default=os.getenv("IQM_DYNAMIC_LICENSE_LOCK_DIR", "/var/run/iqm-dynamic-license"),
        help="Directory holding lease files (default: IQM_DYNAMIC_LICENSE_LOCK_DIR or /var/run/iqm-dynamic-license).",
    )
    parser.add_argument("--resource-name", required=True, help="Dynamic License resource name, e.g. iqm_qc_emerald.")
    parser.add_argument(
        "--holder-id",
        default=_default_holder_id(),
        help="Lease holder identifier (default: $SLURM_JOB_ID[.$SLURM_STEP_ID]).",
    )
    parser.add_argument(
        "--ttl-seconds",
        type=float,
        default=float(os.getenv("IQM_DYNAMIC_LICENSE_LOCK_TTL_SECONDS", str(DEFAULT_TTL_SECONDS))),
        help="Lease TTL for `acquire` (default: IQM_DYNAMIC_LICENSE_LOCK_TTL_SECONDS or 6h).",
    )
    parser.add_argument("command", choices=["acquire", "release", "status"])

    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")
    lock_dir = Path(args.lock_dir)

    if args.command == "acquire":
        granted = acquire(lock_dir, args.resource_name, args.holder_id, ttl_seconds=args.ttl_seconds)
        sys.exit(0 if granted else 1)
    elif args.command == "release":
        released = release(lock_dir, args.resource_name, args.holder_id)
        sys.exit(0 if released else 1)
    else:
        held = is_held(lock_dir, args.resource_name)
        print("held" if held else "free")  # ruff: ignore[print] -- CLI status output, not diagnostic logging
        sys.exit(0 if held else 1)


if __name__ == "__main__":
    main()
