#!/usr/bin/env -S uv run --script --quiet
# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

# /// script
# dependencies = ["nox"]
# ///

"""Nox sessions for Slurm SPANK plugin testing."""

from __future__ import annotations

import argparse
import os

import nox

nox.needs_version = ">=2026.04.10"
nox.options.default_venv_backend = "uv"


@nox.session(reuse_venv=True)
def smoke_tests(session: nox.Session) -> None:
    """Run the SPANK smoke scripts against an active Slurm deployment."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--partition",
        default="quantum",
        help="Slurm partition to target (default: quantum).",
    )
    parser.add_argument(
        "--hook-mode",
        choices=("local", "full"),
        default="local",
        help="Hook mode for test_hook_logs.sh (default: local).",
    )
    parser.add_argument(
        "--require-all-hooks",
        action="store_true",
        help="Require all hook callbacks when running in full mode.",
    )
    parser.add_argument(
        "--test-base-url",
        default=os.environ.get("IQM_BASE_URL"),
        help="Base URL to validate with test_env_injection.sh. Defaults to IQM_BASE_URL.",
    )
    parser.add_argument(
        "--test-tokens-file",
        default=os.environ.get("IQM_TOKENS_FILE"),
        help="Optional tokens file path to validate with test_env_injection.sh.",
    )
    args, posargs = parser.parse_known_args(session.posargs)
    if posargs:
        joined_args = " ".join(posargs)
        session.error(f"Unexpected arguments for the smoke_tests session: {joined_args}")
    if not args.test_base_url:
        session.error("Pass --test-base-url or export IQM_BASE_URL before running smoke_tests.")

    # Paths are relative to the 'spank' directory since this noxfile is located here.
    hook_args = [
        "bash",
        "test/test_hook_logs.sh",
        "--partition",
        args.partition,
        "--hook-mode",
        args.hook_mode,
    ]
    if args.require_all_hooks:
        hook_args.append("--require-all-hooks")

    session.run(*hook_args, external=True)

    env_args = [
        "bash",
        "test/test_env_injection.sh",
        "--partition",
        args.partition,
        "--test-base-url",
        args.test_base_url,
    ]
    if args.test_tokens_file:
        env_args.extend(["--test-tokens-file", args.test_tokens_file])

    session.run(*env_args, external=True)
