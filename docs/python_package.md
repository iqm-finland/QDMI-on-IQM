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
- `IQMBackend`: Qiskit-compatible backend.

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

The package also exposes `IQMBackend`, a thin wrapper around MQT Core's
`QDMIBackend` that loads the packaged IQM QDMI shared library automatically.
This gives you a direct Qiskit entry point for running circuits and using MQT
Core's sampler and estimator primitives with the IQM device.

### Backend Usage

```{code-cell} ipython3
from math import pi

from iqm.qdmi import IQMBackend
from qiskit import QuantumCircuit

backend = IQMBackend(
  base_url="https://resonance.meetiqm.com",
  token="your-api-token",
  qc_alias="garnet:mock",
)

qc = QuantumCircuit(1)
qc.r(pi / 2, 0.0, 0)
qc.measure_all()

result = backend.run(qc, shots=128).result()
print(result.get_counts())
```

If constructor arguments are omitted, `IQMBackend` falls back to the following
environment variables when available:

- `IQM_BASE_URL`
- `RESONANCE_API_KEY`
- `IQM_QC_ID`
- `IQM_QC_ALIAS`

### Sampler and Estimator Primitives

`IQMBackend` provides small helpers for constructing sampler and estimator primitives bound
to the backend instance.

```{code-cell} ipython3
from math import pi

from iqm.qdmi import IQMBackend
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

backend = IQMBackend()

sampler_circuit = QuantumCircuit(1)
sampler_circuit.r(pi / 2, 0.0, 0)
sampler_circuit.measure_all()

sampler_result = backend.sampler().run([(sampler_circuit,)], shots=128).result()[0]
print(sampler_result.data.meas.get_counts())

estimator_circuit = QuantumCircuit(1)
estimator_circuit.r(pi / 2, 0.0, 0)
observable = SparsePauliOp("Z")

estimator_result = backend.estimator().run([(estimator_circuit, observable)]).result()[0]
print(estimator_result.data.evs)
print(estimator_result.data.stds)
```
