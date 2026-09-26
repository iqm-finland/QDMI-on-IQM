---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Using Qiskit to Run Quantum Workloads on IQM Hardware via QDMI-on-IQM

The [`iqm-qdmi` Python package](python_package.md) includes a wrapper for the
[IQM QDMI Device library](usage.md) that integrates it with Qiskit. This wrapper
is implemented in the {py:mod}`iqm.qdmi.qiskit` submodule and is based on the
open-source, MIT-licensed MQT Core library. To use the wrapper, make sure to
install the `iqm-qdmi` package with the `qiskit` extra:

```console
uv pip install iqm-qdmi[qiskit]
```

Then, the {py:class}`~iqm.qdmi.qiskit.IQMBackend` class can be imported from
{py:mod}`iqm.qdmi.qiskit` and used as a drop-in replacement for any Qiskit
backend.

```{code-cell} ipython3
from iqm.qdmi.qiskit import IQMBackend
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile

backend = IQMBackend("iqm.emerald.mock")
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

Select any stable ID from the
[installed catalogue](usage.md#using-the-device-with-mqt-core).
`IQMBackend.from_device_id("iqm.emerald.mock", token="…")` provides the same IQM
adapter through Core's factory API. Every backend opens an independent device
session.

`IQMBackend()` keeps the configurable `iqm.default` connection. Explicit
arguments override environment defaults: `IQM_SERVER_URL`, `IQM_TOKEN`,
`IQM_TOKENS_FILE`, `IQM_QC_ID`, and `IQM_QUANTUM_COMPUTER`. `IQM_BASE_URL` and
`IQM_QC_ALIAS` remain legacy aliases, with canonical variables taking
precedence. An explicit quantum computer ID or alias suppresses both environment
selectors.

Named presets use their configured endpoint and quantum computer; routing
environment variables cannot redirect them. Authentication still uses the usual
token or token-file defaults. Explicit arguments and driver configuration can
override manifest values.

IQM JSON represents PRX rotation and phase angles in radians, using the `angle`
and `phase` fields. Like [IQM Client](https://docs.iqm.tech/iqm-client/), the
Qiskit serializer preserves these units. Applications submitting IQM JSON
directly must use the same format; the legacy `angle_t` and `phase_t` fields
expressed angles in turns.

## Circuit Metadata

The IQM JSON serializer preserves `QuantumCircuit.metadata` in the native
program's `metadata` field without modifying the circuit. Empty metadata remains
`{}`. Values follow Python's JSON encoding: dictionaries, lists, tuples (encoded
as arrays), strings, booleans, `None`, integers, and finite floating-point
numbers are supported. Object keys must be strings at every nesting level, so
that keys such as `1` and `"1"` cannot collide after conversion to JSON.

If any value is unsupported (such as NumPy arrays or custom Python objects), or
the metadata has circular references or nonfinite numbers, the serializer drops
the whole metadata with a warning and submits the circuit. Convert such values
explicitly to keep them. This preserves metadata in the submitted IQM program;
it does not add a metadata retrieval API or guarantee that a remote service
returns it in results.

## Sampler and Estimator Primitives

{py:class}`~iqm.qdmi.qiskit.IQMBackend` provides small helpers (see
{py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler` and
{py:meth}`~iqm.qdmi.qiskit.IQMBackend.estimator`) for constructing
{py:class}`~qiskit.primitives.BackendSamplerV2` and
{py:class}`~qiskit.primitives.BackendEstimatorV2` primitives bound to the
backend instance.

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

## CLI Scripts

The package also exposes the `iqm-sampler` and `iqm-estimator` CLI scripts for
executing serialized circuits directly from the shell. For more details on these
utilities and their usage, see the [Python Package Guide](python_package.md).
