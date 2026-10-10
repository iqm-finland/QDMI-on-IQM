---
file_format: mystnb
kernelspec:
  name: python3
language_info:
  name: python
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

The estimator CLI and `offloader.estimate` use Qiskit's default precision of
`1/64`, corresponding to 4,096 shots per measurement circuit. See the
[primitive options](qiskit.md#sampler-and-estimator-primitives).

## Programmatic Offloading with the `offloader` Module

For workflows running on a Slurm login node (such as Jupyter notebooks on a
gateway service), the package exposes the {py:mod}`~iqm.qdmi.offloader` module.
It allows you to programmatically submit quantum workloads to the Slurm quantum
queue.

The module provides two primary functions:

- {py:func}`~iqm.qdmi.offloader.sample`: Serializes the circuit to QPY and
  submits a Slurm job using `srun iqm-sampler`. The returned sampler result is
  converted to joint counts across all classical registers in Qiskit's bit
  order.
- {py:func}`~iqm.qdmi.offloader.estimate`: Serializes the ansatz and observable,
  submits a Slurm job using `srun iqm-estimator`, and returns the complete
  `VQEResult` produced by the worker.

Both worker CLIs reserve stdout for one base64-encoded pickle containing the
native Qiskit result; diagnostics belong on stderr. The offloader is intended
for controlled Slurm deployments with trusted workers and serialized inputs. Use
compatible Python and dependency environments on the submitting and worker
nodes. Result loading uses standard pickle without a restricted unpickler.

Both functions support `local=True` to execute in the submitting process instead
of Slurm, and `simulator=True` to select the simulator in either mode.

### Selecting the Slurm Partition

Both functions submit their `srun` jobs to a site-defined partition. Its name is
resolved in this order:

1. The `partition` keyword argument.
2. The `IQM_SLURM_PARTITION` environment variable, which lets an administrator
   set the site's name once for every user. An empty value counts as unset.
3. `quantum`.

```python
counts = sample(qc, shots=512, partition="qc-nodes")
```

Slurm's own `SLURM_PARTITION` has no effect here, because the resolved name is
always passed as an explicit `--partition`, which takes precedence over it.

:::{important}
Use the
[MQT Core Slurm guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
for scheduler setup and license-based injection.
:::

### Sizing the Slurm Allocation

Both functions accept a `nodes` keyword argument, forwarded as `--nodes` and
defaulting to a single node. The worker itself always runs as one task
(`--ntasks=1`), so `nodes` only sizes the allocation for a site whose partition
demands more than one node; it does not distribute or parallelize the workload.

It has no environment fallback: the node count is a per-job resource request
rather than a site-wide constant.

### Shared Jobs Directory

When submitting Slurm workloads, a shared filesystem directory is required to
serialize inputs (QPY/pickle files) for execution on the compute nodes. The
directory path is resolved as follows:

1. Honoring the `IQM_JOBS_DIR` environment variable, if set.
2. Defaulting to a hidden `.qdmi_jobs` folder under the user's home directory
   (`~/.qdmi_jobs`).

Ensure that the jobs directory is located on a shared cluster filesystem
accessible by both the login node and all Slurm compute nodes.

### Selecting a Quantum Computer per Job

Both functions accept optional `qc_id` and `qc_alias` arguments for a remote
job. These non-secret selectors are passed to the `iqm-sampler` and
`iqm-estimator` worker options `--qc-id` and `--qc-alias`. Credentials are
supplied through the job environment.

### Requesting a Slurm License

The optional `licenses` argument is forwarded to `srun` unchanged. The offloader
selects the quantum computer through its explicit arguments. Applications can
also open a catalogue ID with `builtin_driver.open_device()` and pass that
device to `IQMBackend`, as shown in
[IQM on Slurm](spank_plugin.md#run-a-qiskit-job).

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

## Querying the Device Directly

The Qiskit backend covers circuit execution, but a QDMI device also answers
questions about itself. MQT Core discovers the installed device manifest without
importing device implementation code or loading the device library. Open its
stable ID {py:data}`~iqm.qdmi.IQM_QDMI_DEVICE_ID` through the MQT Core QDMI
driver:

```python
from mqt.core.qdmi.builtin_driver import open_device

from iqm.qdmi import IQM_QDMI_DEVICE_ID

device = open_device(IQM_QDMI_DEVICE_ID, token="…", custom2="emerald")

print(device.status())
print(device.supported_program_formats())
```

### Queue Length and Queue Position

The device reports how busy the quantum computer is, so a client can decide
whether to submit now or wait:

```python
waiting = device.queue_length()  # jobs waiting, excluding those executing
```

`queue_length()` returns `None` when the IQM API does not supply a trustworthy
value. A queued job reports how many jobs are ahead of it; querying it refreshes
the job's status first:

```python
ahead = job.queue_position  # None once the job is no longer queued
```

### Retrieving an Existing Job

A job outlives the session that submitted it. Given its ID, a later session can
pick it up again to poll, wait, cancel, or fetch results:

```python
job = device.retrieve_job_by_id("d3416f0a-…")
print(job.check())
```

A retrieved job cannot be resubmitted, and its parameters cannot be changed.

## Temporary development dependencies

This repository currently pins QDMI #509 and MQT Core #2373 commits to exercise
native multi-program jobs and installed device discovery. Replace both pins with
suitable releases and regenerate `uv.lock` before publishing. Remove the
temporary LLVM/MLIR setup from Python CI and Linux wheel-test containers once
MQT Core wheels are available for these APIs. Native-only device builds do not
require LLVM/MLIR.
