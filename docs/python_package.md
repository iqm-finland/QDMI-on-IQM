---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Python Package

The Python package for this project is published on PyPI as `iqm.iqm-qdmi`.
It provides a Python entry point for discovering installation paths that are useful
when integrating the IQM QDMI Device into downstream build systems.

## Install From PyPI

Use `uv pip` to install the package:

```bash
uv pip install iqm.iqm-qdmi
```

## Quick Usage

```{code-cell} ipython3
import iqm.qdmi as qdmi

qdmi.__version__
```

The package exports the following values from `iqm.qdmi`:

- `IQM_QDMI_INCLUDE_DIR`: include directory for C/C++ headers.
- `IQM_QDMI_CMAKE_DIR`: CMake package directory for `find_package` integration.
- `IQM_QDMI_LIBRARY_PATH`: full path to the shared library.
- `__version__`: installed package version.

```{code-cell} ipython3
import iqm.qdmi as qdmi

{
    "version": qdmi.__version__,
    "include_dir": str(qdmi.IQM_QDMI_INCLUDE_DIR),
    "cmake_dir": str(qdmi.IQM_QDMI_CMAKE_DIR),
    "library_path": str(qdmi.IQM_QDMI_LIBRARY_PATH),
}
```

## Command Line Interface

The package installs the `iqm-qdmi` command.

```bash
iqm-qdmi --version
iqm-qdmi --include_dir
iqm-qdmi --cmake_dir
iqm-qdmi --lib_path
```
