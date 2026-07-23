---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Easy Integration with the `iqm-qdmi` Python Package

To ease the distribution and integration of the IQM QDMI Device library, we have
packaged it as a Python module available on PyPI under the name `iqm-qdmi`. This
package provides a convenient way to discover installation paths and metadata
that are useful when integrating the IQM QDMI Device into downstream build
systems, such as CMake-based projects or Python workflows.

## Install From PyPI

Use `uv` (or your Python package manager of choice) to install the package:

```console
uv pip install iqm-qdmi
```

## Quick Usage

The package itself makes the following variables available for import:

- {py:data}`~iqm.qdmi.__version__`: installed package version.
- {py:data}`~iqm.qdmi.IQM_QDMI_INCLUDE_DIR`: include directory for C/C++
  headers.
- {py:data}`~iqm.qdmi.IQM_QDMI_CMAKE_DIR`: CMake package directory for
  `find_package` integration.
- {py:data}`~iqm.qdmi.IQM_QDMI_LIBRARY_PATH`: full path to the shared library.

```{code-cell} ipython3
from iqm.qdmi import __version__, IQM_QDMI_INCLUDE_DIR, IQM_QDMI_CMAKE_DIR, IQM_QDMI_LIBRARY_PATH

print(f"QDMI on IQM version: {__version__}")
print(f"Include directory: {IQM_QDMI_INCLUDE_DIR}")
print(f"CMake directory: {IQM_QDMI_CMAKE_DIR}")
print(f"Library path: {IQM_QDMI_LIBRARY_PATH}")
```

## Command Line Interface

The above values can also be conveniently queried from the command line via the
`iqm-qdmi` entry point.

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

If you install the package with the `qiskit` extra, the following additional
command-line scripts are exposed:

- `iqm-sampler` (see the {py:mod}`~iqm.qdmi.sampler` entry point module):
  Samples a serialized QPY circuit on the specified backend.
- `iqm-estimator` (see the {py:mod}`~iqm.qdmi.estimator` entry point module):
  Variational Quantum Eigensolver (VQE) parameter estimation for a serialized
  ansatz and observable.

### `iqm-sampler` Usage

For example, to execute a QPY circuit file (`bell.qpy`):

```console
iqm-sampler bell.qpy --shots 128
```

### `iqm-estimator` Usage

To run a parameter estimation job:

```console
iqm-estimator ansatz.qpy observable.pkl --maxiter 10
```

## Programmatic Offloading with the `offloader` Module

For workflows running on a Slurm login node (such as Jupyter notebooks on a
gateway service), the package exposes the {py:mod}`~iqm.qdmi.offloader` module.
It allows you to programmatically submit quantum workloads to the Slurm quantum
queue.

The module provides two primary functions:

- {py:func}`~iqm.qdmi.offloader.sample`: Serializes the given circuit to QPY,
  submits a Slurm job using `srun iqm-sampler`, and parses the base64-encoded
  pickled result back to a python dictionary of counts.
- {py:func}`~iqm.qdmi.offloader.estimate`: Serializes the ansatz and observable,
  submits a Slurm job using `srun iqm-estimator`, and parses the base64-encoded
  pickled `VQEResult` to return it, matching the semantics of running `VQE`
  directly against the regular (non-offloaded) estimator.

Both functions support a `local=True` argument for running simulation/hardware
compilation locally (useful for debugging) and a `simulator=True` argument when
submitting Slurm jobs to target simulated devices instead of real QPU hardware.

### Slurm Partition Requirement

Both functions submit their `srun` jobs to a partition named `quantum`
(`srun --partition=quantum ...`). This assumes the cluster has a partition of
that exact name configured, typically gating access to nodes with the quantum
computer exposed as a Slurm GRES resource. Ensure such a `quantum` partition
exists before using the offloader; see the
[SPANK plugin documentation](spank_plugin.md) for an example cluster
configuration.

### Shared Jobs Directory

When submitting Slurm workloads, a shared filesystem directory is required to
serialize inputs (QPY/pickle files) for execution on the compute nodes. The
directory path is resolved as follows:

1. Honoring the `IQM_JOBS_DIR` environment variable, if set.
2. Defaulting to a hidden `.qdmi_jobs` folder under the user's home directory
   (`~/.qdmi_jobs`).

Ensure that the jobs directory is located on a shared cluster filesystem
accessible by both the login node and all Slurm compute nodes.

### Programmatic Sampling Example

```python
from iqm.qdmi.offloader import sample
from qiskit import QuantumCircuit

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

# programmatically offload via Slurm
counts = sample(qc, shots=512, simulator=True)
print("Counts:", counts)
```
