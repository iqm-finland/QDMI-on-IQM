# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Extract and consume a release SDK without IQM backend access."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(*command: str, env: dict[str, str] | None = None) -> str:
    """Run a smoke-test command.

    Returns:
        The command's standard output.
    """
    result = subprocess.run(command, check=True, capture_output=True, text=True, env=env)  # ruff: ignore[subprocess-without-shell-equals-true]
    return result.stdout


def main(archive_path: Path) -> None:
    """Check archive integrity, relocatability, and CMake consumption.

    Raises:
        RuntimeError: If any archive or consumer check fails.
    """
    archive_path = archive_path.resolve()
    checksum_file = archive_path.with_name(f"{archive_path.name}.sha256")
    expected = checksum_file.read_text(encoding="utf-8").split()[0]
    actual = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    if actual != expected:
        msg = f"Checksum mismatch for {archive_path}"
        raise RuntimeError(msg)

    with tempfile.TemporaryDirectory(prefix="iqm-qdmi-sdk-") as temporary:
        work = Path(temporary)
        with tarfile.open(archive_path, "r:gz") as archive:
            for member in archive.getmembers():
                if Path(member.name).parts[0] != "iqm-qdmi-device" or member.issym() or member.islnk():
                    msg = f"Unexpected archive entry: {member.name}"
                    raise RuntimeError(msg)
            archive.extractall(work, filter="data")
        prefix = work / "relocated" / "iqm-qdmi-device"
        prefix.parent.mkdir()
        shutil.move(str(work / "iqm-qdmi-device"), prefix)
        if not (prefix / "include/iqm_qdmi/device.h").is_file():
            msg = "The SDK header is missing"
            raise RuntimeError(msg)
        if not (prefix / "share/cmake/iqm-qdmi-device/iqm-qdmi-device-config.cmake").is_file():
            msg = "The CMake package is missing"
            raise RuntimeError(msg)
        if not (prefix / "licenses/QDMI-on-IQM.txt").is_file():
            msg = "The project license is missing"
            raise RuntimeError(msg)

        build = work / "consumer-build"
        run("cmake", "-S", str(ROOT / "test/sdk/consumer"), "-B", str(build), f"-DCMAKE_PREFIX_PATH={prefix}")
        run("cmake", "--build", str(build), "--config", "Release")
        executable = build / "Release/sdk-smoke.exe" if sys.platform == "win32" else build / "sdk-smoke"
        env = os.environ.copy()
        if sys.platform == "win32":
            env["PATH"] = f"{prefix / 'bin'}{os.pathsep}{env.get('PATH', '')}"
        run(str(executable), env=env)

        if sys.platform.startswith("linux"):
            for library in (prefix / "lib").glob("*.so*"):
                dynamic = run("readelf", "-d", str(library))
                if "$ORIGIN" not in dynamic or temporary in dynamic:
                    msg = f"Linux SDK has a nonrelocatable RPATH: {library}"
                    raise RuntimeError(msg)
                if "not found" in run("ldd", str(library)):
                    msg = f"Linux SDK has unresolved runtime dependencies: {library}"
                    raise RuntimeError(msg)
                versions = run("readelf", "--version-info", str(library))
                for major, minor in re.findall(r"\bGLIBC_(\d+)\.(\d+)\b", versions):
                    if (int(major), int(minor)) > (2, 28):
                        msg = f"Linux SDK exceeds the glibc 2.28 baseline: {library}: GLIBC_{major}.{minor}"
                        raise RuntimeError(msg)
        elif sys.platform == "darwin":
            for library in (prefix / "lib").glob("*.dylib"):
                linked = run("otool", "-L", str(library))
                if temporary in linked or "/opt/homebrew/" in linked or "/usr/local/" in linked:
                    msg = f"macOS SDK has a machine-specific library path: {library}"
                    raise RuntimeError(msg)
                run("codesign", "--verify", str(library))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        msg = "usage: smoke_archive.py ARCHIVE.tar.gz"
        raise SystemExit(msg)
    main(Path(sys.argv[1]))
