---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Easy Integration with the `iqm-qdmi` Python Package

To ease the distribution and integration of the IQM QDMI Device library, we have packaged it as a Python module available on PyPI under the name `iqm-qdmi`.
This package provides a convenient way to discover installation paths and metadata that are useful when integrating the IQM QDMI Device into downstream build systems, such as CMake-based projects or Python workflows.

## Install From PyPI

Use `uv` (or your Python package manager of choice) to install the package:

```console
uv pip install iqm-qdmi
```

## Quick Usage

The package itself makes the following variables available for import:

- {py:data}`~iqm.qdmi.__version__`: installed package version.
- {py:data}`~iqm.qdmi.IQM_QDMI_INCLUDE_DIR`: include directory for C/C++ headers.
- {py:data}`~iqm.qdmi.IQM_QDMI_CMAKE_DIR`: CMake package directory for `find_package` integration.
- {py:data}`~iqm.qdmi.IQM_QDMI_LIBRARY_PATH`: full path to the shared library.

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

## Sampler and Estimator CLI Utilities

If you install the package with the `qiskit` extra, the following additional command-line scripts are exposed:

- `iqm-sampler`: Samples a serialized QPY circuit on the specified backend.
- `iqm-estimator`: Variational Quantum Eigensolver (VQE) parameter estimation for a serialized ansatz and observable.

### `iqm-sampler` Usage

For example, to execute a QPY circuit file (`bell.qpy`):

```console
$ iqm-sampler bell.qpy --shots 128
```

### `iqm-estimator` Usage

To run a parameter estimation job:

```console
$ iqm-estimator ansatz.qpy observable.pkl --maxiter 10
```
