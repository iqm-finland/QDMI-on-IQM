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

import contextlib
import os
import shutil
import tempfile
from typing import TYPE_CHECKING

import nox

if TYPE_CHECKING:
    from collections.abc import Generator, Sequence

nox.needs_version = ">=2026.04.10"
nox.options.default_venv_backend = "uv"

PYTHON_ALL_VERSIONS = ["3.10", "3.11", "3.12", "3.13", "3.14"]

if os.environ.get("CI", None):
    nox.options.error_on_missing_interpreters = True


@contextlib.contextmanager
def preserve_lockfile() -> Generator[None]:
    """Preserve the lockfile by moving it to a temporary directory."""
    with tempfile.TemporaryDirectory() as temp_dir_name:
        shutil.move("uv.lock", f"{temp_dir_name}/uv.lock")
        try:
            yield
        finally:
            shutil.move(f"{temp_dir_name}/uv.lock", "uv.lock")


@nox.session(reuse_venv=True, default=True)
def lint(session: nox.Session) -> None:
    """Run the linter."""
    if shutil.which("prek") is None:
        session.install("prek")

    session.run("prek", "run", "--all-files", *session.posargs, external=True)


def _run_tests(
    session: nox.Session,
    *,
    install_args: Sequence[str] = (),
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
        "--no-dev",
        "--group",
        "test",
        *install_args,
        "pytest",
        *pytest_run_args,
        *session.posargs,
        "--cov-config=pyproject.toml",
        env=env,
    )


@nox.session(python=PYTHON_ALL_VERSIONS, reuse_venv=True, default=True)
def tests(session: nox.Session) -> None:
    """Run the test suite."""
    _run_tests(session)


@nox.session(python=PYTHON_ALL_VERSIONS, reuse_venv=True, venv_backend="uv", default=True)
def minimums(session: nox.Session) -> None:
    """Test the minimum versions of dependencies."""
    with preserve_lockfile():
        _run_tests(
            session,
            install_args=["--resolution=lowest-direct"],
            pytest_run_args=["-Wdefault"],
        )
        env = {"UV_PROJECT_ENVIRONMENT": session.virtualenv.location}
        session.run("uv", "tree", "--frozen", env=env)


@nox.session(reuse_venv=True)
def docs(session: nox.Session) -> None:
    """Build docs via CMake."""
    cmake = shutil.which("cmake") or shutil.which("cmake3")
    if cmake is None:
        session.install("cmake")
        cmake = shutil.which("cmake") or shutil.which("cmake3") or "cmake"

    session.run(
        cmake,
        "-S",
        ".",
        "-B",
        "build",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_IQM_QDMI_DOCS=ON",
        "-DBUILD_IQM_QDMI_TESTS=OFF",
        *session.posargs,
        external=True,
    )
    session.run(cmake, "--build", "build", "--target", "iqm_qdmi_device_docs", "--parallel", external=True)


if __name__ == "__main__":
    nox.main()
