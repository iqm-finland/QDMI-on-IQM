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

"""Assemble a relocatable C++ SDK from CMake's installed components."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINUX_SYSTEM_LIBRARIES = re.compile(
    r"^(?:ld-linux[^/]*|ld64[^/]*|lib(?:c|m|pthread|dl|rt|util|resolv|nsl|stdc\+\+|gcc_s)\.so(?:\..*)?)$"
)
WINDOWS_SYSTEM_LIBRARIES = re.compile(
    r"^(?:api-ms-win-.*|ext-ms-.*|ucrtbase|vcruntime\d*|msvcp\d*|concrt\d*)\.dll$",
    re.IGNORECASE,
)
SOURCE_LICENSES = {
    "QDMI": ("qdmi-src", "LICENSE"),
    "nlohmann-json": ("nlohmann_json-src", "LICENSE.MIT"),
    "CPR": ("cpr-src", "LICENSE"),
    "curl": ("curl-src", "COPYING"),
    "zlib": ("zlib-src", "LICENSE.md"),
}


def run(*args: str) -> str:
    """Run a native packaging tool.

    Returns:
        The tool's standard output.

    Raises:
        RuntimeError: If the packaging tool fails.
    """
    result = subprocess.run(args, capture_output=True, text=True, check=False)  # ruff: ignore[subprocess-without-shell-equals-true]
    if result.returncode != 0:
        msg = f"Command failed: {args!r}\n{result.stdout}\n{result.stderr}"
        raise RuntimeError(msg)
    return result.stdout


def is_system_dependency(path: Path, platform: str) -> bool:
    """Identify runtimes provided by the supported operating system ABI.

    Returns:
        Whether the dependency is supplied by the operating system.
    """
    if platform == "linux":
        return bool(LINUX_SYSTEM_LIBRARIES.fullmatch(path.name))
    if platform == "macos":
        return path.as_posix().startswith(("/usr/lib/", "/System/Library/"))
    return bool(WINDOWS_SYSTEM_LIBRARIES.fullmatch(path.name)) or "windows/system32/" in path.as_posix().lower()


def find_device_library(prefix: Path, platform: str) -> Path:
    """Find the platform's installed device shared library.

    Returns:
        The installed library path.

    Raises:
        RuntimeError: If the library is missing or ambiguous.
    """
    patterns = {
        "linux": "lib/libiqm-qdmi-device.so",
        "macos": "lib/libiqm-qdmi-device.dylib",
        "windows": "bin/iqm-qdmi-device.dll",
    }
    matches = list(prefix.glob(patterns[platform]))
    if len(matches) != 1:
        msg = f"Expected exactly one installed device library at {patterns[platform]}"
        raise RuntimeError(msg)
    return matches[0]


def scan_dependencies(library: Path, output: Path) -> list[Path]:
    """Collect direct and transitive native dependencies with CMake.

    Returns:
        Paths of resolved dependencies.
    """
    run(
        "cmake",
        f"-DSDK_LIBRARY={library}",
        f"-DSDK_OUTPUT={output}",
        "-P",
        str(ROOT / "cmake/CollectSdkRuntimeDependencies.cmake"),
    )
    return [Path(line) for line in output.read_text(encoding="utf-8").splitlines() if line]


def copy_dependencies(dependencies: list[Path], destination: Path, platform: str) -> dict[str, Path]:
    """Copy redistributable dependencies into the device library's directory.

    Returns:
        Mapping from bundled filenames to their original paths.

    Raises:
        RuntimeError: If two dependencies have conflicting filenames.
    """
    bundled: dict[str, Path] = {}
    destination.mkdir(parents=True, exist_ok=True)
    for dependency in dependencies:
        if is_system_dependency(dependency, platform):
            continue
        name = dependency.name
        if name in bundled:
            if dependency.read_bytes() != bundled[name].read_bytes():
                msg = f"Conflicting runtime libraries named {name}"
                raise RuntimeError(msg)
            continue
        target = destination / name
        if target.exists() and dependency.resolve() != target.resolve():
            msg = f"Runtime dependency would overwrite {target}"
            raise RuntimeError(msg)
        if not target.exists():
            shutil.copy2(dependency, target)
        bundled[name] = dependency
    return bundled


def fix_linux_paths(library: Path, bundled: dict[str, Path]) -> None:
    """Make each ELF search for its dependencies beside itself.

    Raises:
        RuntimeError: If patching a library fails.
    """
    for binary in [library, *(library.parent / name for name in bundled)]:
        run("patchelf", "--set-rpath", "$ORIGIN", str(binary))
        if "$ORIGIN" not in run("patchelf", "--print-rpath", str(binary)):
            msg = f"Failed to make {binary} relocatable"
            raise RuntimeError(msg)


def fix_macos_paths(library: Path, bundled: dict[str, Path]) -> None:
    """Rewrite bundled dylib references to paths relative to each binary."""
    for binary in [library, *(library.parent / name for name in bundled)]:
        output = run("otool", "-L", str(binary))
        for line in output.splitlines()[1:]:
            old = line.strip().split(" (compatibility version", maxsplit=1)[0]
            name = Path(old).name
            if name in bundled:
                run("install_name_tool", "-change", old, f"@loader_path/{name}", str(binary))
        run("install_name_tool", "-id", f"@rpath/{binary.name}", str(binary))
        run("codesign", "--force", "--sign", "-", str(binary))


def copy_licenses(prefix: Path, build_dir: Path, dependencies: dict[str, Path], platform: str) -> None:
    """Preserve project and dependency license texts in the archive.

    Raises:
        RuntimeError: If a bundled dependency has no available license text.
    """
    licenses = prefix / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "QDMI-on-IQM.txt")
    for name, (source, filename) in SOURCE_LICENSES.items():
        license_file = build_dir / "_deps" / source / filename
        if license_file.exists():
            shutil.copy2(license_file, licenses / f"{name}.txt")
        elif name in {"QDMI", "nlohmann-json", "CPR"} or (name == "curl" and platform != "macos"):
            msg = f"Missing license text for {name}: {license_file}"
            raise RuntimeError(msg)

    if platform == "linux":
        for name, source in dependencies.items():
            package = run("rpm", "-qf", "--qf", "%{NAME}", str(source)).strip()
            license_paths = run("rpm", "-ql", package).splitlines()
            license_files = [Path(path) for path in license_paths if "/licenses/" in path and Path(path).is_file()]
            if not license_files:
                msg = f"No installed license text for {name} ({package})"
                raise RuntimeError(msg)
            for index, license_file in enumerate(license_files):
                shutil.copy2(license_file, licenses / f"{package}-{index}-{license_file.name}")
    else:
        for name, source in dependencies.items():
            license_file = next(
                (
                    file
                    for directory in list(source.parents)[:4]
                    for pattern in ("LICENSE*", "COPYING*")
                    for file in directory.glob(pattern)
                    if file.is_file()
                ),
                None,
            )
            if license_file is None:
                msg = f"No license text found beside bundled dependency {name}: {source}"
                raise RuntimeError(msg)
            shutil.copy2(license_file, licenses / f"{name}-{license_file.name}")


def make_archive(prefix: Path, output: Path) -> None:
    """Archive the install prefix with relative paths and a stable root."""
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz") as archive:
        for path in sorted(prefix.rglob("*")):
            archive.add(path, arcname=Path("iqm-qdmi-device") / path.relative_to(prefix), recursive=False)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    (output.parent / f"{output.name}.sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")


def project_version() -> str:
    """Read the native library version used to name release assets.

    Returns:
        The project version.

    Raises:
        RuntimeError: If CMake's project version cannot be found.
    """
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(\s*iqm-qdmi-device\s+LANGUAGES C CXX\s+VERSION (\d+\.\d+\.\d+)", source)
    if match is None:
        msg = "Cannot read the iqm-qdmi-device project version"
        raise RuntimeError(msg)
    return match.group(1)


def main() -> None:
    """Package the staged CMake installation for one native platform.

    Raises:
        RuntimeError: If required installed components are absent.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--platform", choices=("linux", "macos", "windows"), required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--target-triple", required=True)
    args = parser.parse_args()

    prefix = args.install_root.resolve()
    build_dir = args.build_dir.resolve()
    library = find_device_library(prefix, args.platform)
    if not (prefix / "include/iqm_qdmi").is_dir() or not (prefix / "share/cmake/iqm-qdmi-device").is_dir():
        msg = "Incomplete CMake development installation"
        raise RuntimeError(msg)
    scan_file = build_dir / "sdk-runtime-dependencies.txt"
    dependencies = scan_dependencies(library, scan_file)
    bundled = copy_dependencies(dependencies, library.parent, args.platform)
    if args.platform == "linux":
        fix_linux_paths(library, bundled)
    elif args.platform == "macos":
        fix_macos_paths(library, bundled)
    copy_licenses(prefix, build_dir, bundled, args.platform)
    output = args.output_dir.resolve() / f"iqm-qdmi-device_v{project_version()}_{args.target_triple}.tar.gz"
    make_archive(prefix, output)


if __name__ == "__main__":
    main()
