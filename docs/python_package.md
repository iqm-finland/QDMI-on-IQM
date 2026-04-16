---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Python Package

The Python package for this project is published on PyPI as `iqm-qdmi`.
It provides a Python entry point for discovering installation paths that are useful
when integrating the IQM QDMI Device into downstream build systems.

## Install From PyPI

Use `uv` (or your Python package manager of choice) to install the package:

```bash
uv pip install iqm-qdmi
```

## Quick Usage

The package itself makes the following variables available for import:

- `__version__`: installed package version.
- `IQM_QDMI_INCLUDE_DIR`: include directory for C/C++ headers.
- `IQM_QDMI_CMAKE_DIR`: CMake package directory for `find_package` integration.
- `IQM_QDMI_LIBRARY_PATH`: full path to the shared library.

```{code-cell} ipython3
from iqm.qdmi import __version__, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_CMAKE_DIR, IQM_QDMI_LIBRARY_PATH

print(f"QDMI on IQM version: {__version__}")
print(f"Include directory: {IQM_QDMI_INCLUDE_DIR}")
print(f"CMake directory: {IQM_QDMI_CMAKE_DIR}")
print(f"Library path: {IQM_QDMI_LIBRARY_PATH}")
```

## Command Line Interface

The above values can also be conveniently queried from the command line via the `iqm-qdmi` entry point.

```{code-cell} ipython3
!iqm-qdmi --help
```

```{code-cell} ipython3
!iqm-qdmi --version
```

```{code-cell} ipython3
!iqm-qdmi --include_dir
```

```{code-cell} ipython3
!iqm-qdmi --cmake_dir
```

```{code-cell} ipython3
!iqm-qdmi --lib_path
```
