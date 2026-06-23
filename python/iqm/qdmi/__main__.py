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
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request
from functools import partial

from . import IQM_QDMI_CMAKE_DIR, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_LIBRARY_PATH, __version__


def list_devices() -> None:
    """Query and list available IQM quantum computers."""
    base_url = os.getenv("IQM_BASE_URL") or "https://resonance.iqm.tech"
    token = os.getenv("IQM_TOKEN")
    tokens_file = os.getenv("IQM_TOKENS_FILE")

    if not token and tokens_file:
        try:
            with pathlib.Path(tokens_file).open("r", encoding="utf-8") as f:
                data = json.load(f)
                token = data.get("access_token")
        except (OSError, json.JSONDecodeError) as e:
            print(f"Error reading tokens file '{tokens_file}': {e}", file=sys.stderr)
            sys.exit(1)

    url = f"{base_url.rstrip('/')}/api/v1/quantum-computers"
    headers = {}
    if token:
        headers["Authorization"] = f"Bearer {token}"

    req = urllib.request.Request(url, headers=headers)  # noqa: S310
    try:
        with urllib.request.urlopen(req) as response:  # noqa: S310
            resp_data = response.read()
    except urllib.error.URLError as e:
        print(f"Error querying endpoint {url}: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        data = json.loads(resp_data.decode())
    except json.JSONDecodeError as e:
        print(f"Error parsing response from {url}: {e}", file=sys.stderr)
        sys.exit(1)

    qcs = data.get("quantum_computers", [])
    if not qcs:
        print("No quantum computers available.")
        return
    print(f"Available IQM Quantum Computers (endpoint: {base_url}):")
    for qc in qcs:
        alias = qc.get("alias", "N/A")
        qc_id = qc.get("id", "N/A")
        status = qc.get("status", "N/A")
        print(f"  - {alias} (ID: {qc_id}, Status: {status})")


def main() -> None:
    """Entry point for the iqm-qdmi command line interface.

    This function is called when running the `iqm-qdmi` script.

    .. code-block:: bash

        iqm-qdmi [--version] [--include_dir] [--cmake_dir] [--lib_path] [--list-devices]

    It provides the following command line options:

    - :code:`--version`: Print the version and exit.
    - :code:`--include_dir`: Print the path to the iqm-qdmi C/C++ include directory.
    - :code:`--cmake_dir`: Print the path to the iqm-qdmi CMake module directory.
    - :code:`--lib_path`: Print the path to the iqm-qdmi shared library.
    - :code:`--list-devices`: Query and print available IQM quantum computers.
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
    group.add_argument(
        "--list-devices",
        action="store_true",
        help="Query and print available IQM quantum computers",
    )

    args = parser.parse_args()

    if args.include_dir:
        print(IQM_QDMI_INCLUDE_DIR)
    elif args.cmake_dir:
        print(IQM_QDMI_CMAKE_DIR)
    elif args.lib_path:
        print(IQM_QDMI_LIBRARY_PATH)
    elif args.list_devices:
        list_devices()
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
