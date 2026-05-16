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

"""Command line interface for the IQM QDMI device library."""

import argparse
import shutil
import subprocess
import sys
from functools import partial
from pathlib import Path

from . import IQM_QDMI_CMAKE_DIR, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_LIBRARY_PATH, __version__


def _find_project_root(start_path: Path) -> Path | None:
    for candidate in (start_path, *start_path.parents):
        if (candidate / "pyproject.toml").is_file() and (candidate / "examples").is_dir():
            return candidate
    return None


def _run_uv_command(command: list[str], *, cwd: Path | None = None) -> int:
    """Run a uv command assembled from validated CLI inputs.

    Returns:
        The delegated uv process exit code.
    """
    return subprocess.run(command, check=False, cwd=cwd).returncode  # noqa: S603


def _run_example(example: str, example_args: list[str]) -> int:
    example_path = Path(example)
    if not example_path.is_file():
        msg = f"Example script not found: {example}"
        raise SystemExit(msg)

    uv_executable = shutil.which("uv")
    if uv_executable is None:
        msg = "Could not find the uv executable required to launch examples."
        raise SystemExit(msg)

    project_root = _find_project_root(Path.cwd())
    if project_root is not None:
        command = [
            uv_executable,
            "run",
            "--with-editable",
            ".",
            str(example_path.resolve().relative_to(project_root)),
            *example_args,
        ]
        return _run_uv_command(command, cwd=project_root)

    command = [uv_executable, "run", "--script", str(example_path.resolve()), *example_args]
    return _run_uv_command(command)


def main() -> None:
    """Entry point for the iqm-qdmi command line interface.

    This function is called when running the `iqm-qdmi` script.

    .. code-block:: bash

        iqm-qdmi [--version] [--include_dir] [--cmake_dir] [--lib_path]
        iqm-qdmi examples/ghz.py [--backend sim]

    It provides the following command line options:

    - :code:`--version`: Print the version and exit.
    - :code:`--include_dir`: Print the path to the iqm-qdmi C/C++ include directory.
    - :code:`--cmake_dir`: Print the path to the iqm-qdmi CMake module directory.
    - :code:`--lib_path`: Print the path to the iqm-qdmi shared library.
    - :code:`examples/<name>.py`: Launch a local example script via `uv`.

    Raises:
        SystemExit: If example launching fails or when propagating the delegated example exit code.
    """
    make_parser = partial(
        argparse.ArgumentParser, prog="iqm-qdmi", description="Command line interface for the QDMI on IQM library."
    )
    if sys.version_info >= (3, 14):
        make_parser = partial(make_parser, suggest_on_error=True)

    parser = make_parser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--version",
        action="version",
        version=f"{__version__}",
    )
    group.add_argument(
        "--include_dir",
        action="store_true",
        help="Print the path to the iqm-qdmi C/C++ include directory",
    )
    group.add_argument(
        "--cmake_dir",
        action="store_true",
        help="Print the path to the iqm-qdmi CMake module directory",
    )
    group.add_argument(
        "--lib_path",
        action="store_true",
        help="Print the path to the iqm-qdmi shared library",
    )
    parser.add_argument(
        "example",
        nargs="?",
        help="Launch a local example script, for example examples/ghz.py",
    )
    parser.add_argument(
        "example_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed through to the example script",
    )

    args = parser.parse_args()

    if args.example is not None:
        raise SystemExit(_run_example(args.example, args.example_args))

    if args.include_dir:
        print(IQM_QDMI_INCLUDE_DIR)
    elif args.cmake_dir:
        print(IQM_QDMI_CMAKE_DIR)
    elif args.lib_path:
        print(IQM_QDMI_LIBRARY_PATH)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
