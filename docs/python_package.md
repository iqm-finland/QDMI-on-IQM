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

## Qiskit Integration

The package includes a wrapper for the IQM QDMI Device library that integrates it with Qiskit.
This wrapper is implemented in the {py:mod}`iqm.qdmi.qiskit` submodule and is based on the open-source, MIT-licensed MQT Core library.
To use the wrapper, make sure to install the `iqm-qdmi` package with the `qiskit` extra:

```bash
uv pip install iqm-qdmi[qiskit]
```

Then, the {py:class}`~iqm.qdmi.qiskit.IQMBackend` class can be imported from {py:mod}`iqm.qdmi.qiskit` and used as a drop-in replacement for any Qiskit backend.

```{code-cell} ipython3
from iqm.qdmi.qiskit import IQMBackend
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile

backend = IQMBackend(
  base_url="https://resonance.iqm.tech",
  qc_alias="emerald:mock",
)
```

```{code-cell} ipython3
qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

transpiled_qc = transpile(qc, backend)
result = backend.run(transpiled_qc, shots=128).result()
print(result.get_counts())
```

If no API token is explicitly provided, like in the example above, the wrapper will attempt to read it from the `IQM_TOKEN` or `RESONANCE_API_KEY` environment variables.

### Sampler and Estimator Primitives

{py:class}`~iqm.qdmi.qiskit.IQMBackend` provides small helpers (see {py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler` and {py:meth}`~iqm.qdmi.qiskit.IQMBackend.estimator`) for constructing {py:class}`~qiskit.primitives.BaseSamplerV2` and {py:class}`~qiskit.primitives.BaseEstimatorV2` primitives bound
to the backend instance.

```{code-cell} ipython3
sampler_job = backend.sampler().run([(transpiled_qc,)], shots=128)
counts = sampler_job.result()[0].data["meas"].get_counts()
print(f"Counts: {counts}")
```

```{code-cell} ipython3
from qiskit.quantum_info import SparsePauliOp

transpiled_qc.remove_final_measurements(inplace=True)
observable = SparsePauliOp("Z" * backend.num_qubits)

estimator_job = backend.estimator().run([(transpiled_qc, observable)])
data = estimator_job.result()[0].data
print(f"Expectation values: {data['evs']}")
print(f"Standard deviations: {data['stds']}")
```

### End-to-End Workflow Examples

The repository also ships larger, workflow examples built on top of these primitives.
See the [workflow guide](use_cases.md) for:

- MQT Bench sampler workflows in `test/use_cases/mqt_bench/test_mqt_bench.py`
- A QSCI estimator-and-sampler workflow in `test/use_cases/qsci/test_qsci.py`

Those use-case workflows are intentionally separate from the lightweight tests in `test/python/`.
Run them explicitly with:

```bash
uv run --group showcase pytest test/use_cases
```

:::note
The QSCI workflow depends on PySCF, which is [not supported on Windows](https://pyscf.org/user/install.html).
:::
