#!/usr/bin/env -S uv run --script --quiet
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

    license_args = [
        "bash",
        "test/test_license_alignment.sh",
        "--partition",
        args.partition,
    ]
    session.run(*license_args, external=True)


@nox.session(reuse_venv=True)
def resonance_tests(session: nox.Session) -> None:
    """Run a real workflow on Resonance through the SPANK plugin."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--partition",
        default="quantum",
        help="Slurm partition to target (default: quantum).",
    )
    parser.add_argument(
        "--test-base-url",
        default="https://resonance.iqm.tech",
        help="Resonance endpoint to target (default: https://resonance.iqm.tech).",
    )
    parser.add_argument(
        "--test-qc-alias",
        default="emerald:mock",
        help="Quantum computer alias to target (default: emerald:mock).",
    )
    parser.add_argument(
        "--test-tokens-file",
        default=os.environ.get("IQM_TOKENS_FILE"),
        help="Optional tokens file path for authentication.",
    )
    args, posargs = parser.parse_known_args(session.posargs)
    if posargs:
        joined_args = " ".join(posargs)
        session.error(f"Unexpected arguments for the resonance_tests session: {joined_args}")

    if not os.environ.get("IQM_TOKEN") and not args.test_tokens_file:
        session.error(
            "Either export IQM_TOKEN or pass --test-tokens-file / export IQM_TOKENS_FILE "
            "before running resonance_tests."
        )

    # Paths are relative to the 'spank' directory since this noxfile is located here.
    workflow_args = [
        "bash",
        "test/test_resonance_workflow.sh",
        "--partition",
        args.partition,
        "--test-base-url",
        args.test_base_url,
        "--test-qc-alias",
        args.test_qc_alias,
    ]
    if args.test_tokens_file:
        workflow_args.extend(["--test-tokens-file", args.test_tokens_file])

    session.run(*workflow_args, external=True)
