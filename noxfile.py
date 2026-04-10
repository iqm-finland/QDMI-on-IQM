#!/usr/bin/env -S uv run --script --quiet
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

# /// script
# dependencies = ["nox"]
# ///

"""Nox sessions."""

from __future__ import annotations

import argparse
import os
import shutil
from typing import TYPE_CHECKING

import nox

if TYPE_CHECKING:
    from collections.abc import Sequence

nox.needs_version = ">=2025.11.12"
nox.options.default_venv_backend = "uv"

PYTHON_ALL_VERSIONS = ["3.10", "3.11", "3.12", "3.13", "3.14"]

if os.environ.get("CI", None):
    nox.options.error_on_missing_interpreters = True


@nox.session(reuse_venv=True, default=True)
def lint(session: nox.Session) -> None:
    """Run the linter."""
    if shutil.which("prek") is None:
        session.install("prek")

    session.run("prek", "run", "--all-files", *session.posargs, external=True)


def _run_tests(
    session: nox.Session,
    *,
    extra_command: Sequence[str] = (),
    pytest_run_args: Sequence[str] = (),
) -> None:
    env = {"UV_PROJECT_ENVIRONMENT": session.virtualenv.location}

    if extra_command:
        session.run(*extra_command, env=env)
    if "--cov" in session.posargs:
        # try to use the lighter-weight `sys.monitoring` coverage core
        env["COVERAGE_CORE"] = "sysmon"

    session.run(
        "uv",
        "run",
        "pytest",
        "test/python",
        *pytest_run_args,
        *session.posargs,
        "--cov-config=pyproject.toml",
        env=env,
    )


@nox.session(python=PYTHON_ALL_VERSIONS, reuse_venv=True, default=True)
def tests(session: nox.Session) -> None:
    """Run the test suite."""
    _run_tests(session)


@nox.session(reuse_venv=True)
def docs(session: nox.Session) -> None:
    """Build docs via CMake (Doxygen target: iqm_qdmi_device_docs)."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-dir",
        default="build/",
        help="CMake build directory (default: build/)",
    )
    parser.add_argument(
        "--build-type",
        default="Release",
        help="CMake build type (default: Release)",
    )
    parser.add_argument(
        "--target",
        default="iqm_qdmi_device_docs",
        help="CMake target to build (default: iqm_qdmi_device_docs)",
    )
    args, posargs = parser.parse_known_args(session.posargs)

    cmake = shutil.which("cmake") or shutil.which("cmake3")
    if cmake is None:
        session.install("cmake")
        cmake = shutil.which("cmake") or shutil.which("cmake3") or "cmake"
    if shutil.which("ninja") is None:
        session.install("ninja")
    if shutil.which("doxygen") is None:
        session.error("Doxygen is required to build the docs")

    session.run(
        cmake,
        "-S",
        ".",
        "-B",
        args.build_dir,
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        "-DBUILD_IQM_QDMI_DOCS=ON",
        "-DBUILD_IQM_QDMI_TESTS=OFF",
        *posargs,
        external=True,
    )
    session.run(cmake, "--build", args.build_dir, "--target", args.target, "--parallel", external=True)


if __name__ == "__main__":
    nox.main()
