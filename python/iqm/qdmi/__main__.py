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
import sys
from functools import partial

from . import IQM_QDMI_CMAKE_DIR, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_LIBRARY_PATH, __version__


def main() -> None:
    """Entry point for the iqm-qdmi command line interface.

    This function is called when running the `iqm-qdmi` script.

    .. code-block:: bash

        iqm-qdmi [--version] [--include_dir] [--cmake_dir] [--lib_path]

    It provides the following command line options:

    - :code:`--version`: Print the version and exit.
    - :code:`--include_dir`: Print the path to the iqm-qdmi C/C++ include directory.
    - :code:`--cmake_dir`: Print the path to the iqm-qdmi CMake module directory.
    - :code:`--lib_path`: Print the path to the iqm-qdmi shared library.
    """
    make_parser = partial(
        argparse.ArgumentParser, prog="iqm-qdmi", description="Command line interface for the IQM QDMI device library."
    )
    if sys.version_info >= (3, 14):
        make_parser = partial(make_parser, suggest_on_error=True)

    parser = make_parser()
    parser.add_argument(
        "--version",
        action="version",
        version=f"{__version__}",
    )
    parser.add_argument(
        "--include_dir",
        action="store_true",
        help="Print the path to the iqm-qdmi C/C++ include directory",
    )
    parser.add_argument(
        "--cmake_dir",
        action="store_true",
        help="Print the path to the iqm-qdmi CMake module directory",
    )
    parser.add_argument(
        "--lib_path",
        action="store_true",
        help="Print the path to the iqm-qdmi shared library",
    )

    args = parser.parse_args()

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
