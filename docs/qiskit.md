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

Explicit arguments to `IQMBackend(...)` take precedence over `IQM_SERVER_URL`,
`IQM_TOKEN`, `IQM_TOKENS_FILE`, `IQM_QC_ID`, and `IQM_QUANTUM_COMPUTER` from the
environment. `IQM_BASE_URL` and `IQM_QC_ALIAS` remain supported as legacy
aliases. Canonical variables take precedence over their legacy aliases, which
take precedence over the registered device default.

The wrapper registers the packaged IQM QDMI device as a fallback under the
stable ID `iqm.default` with the standard Resonance endpoint as its default. An
existing configured definition with that ID is preserved, including its
endpoint. Every backend opens a fresh device session with its own configuration.

IQM JSON represents PRX rotation and phase angles in radians, using the `angle`
and `phase` fields. Like [IQM Client](https://docs.iqm.tech/iqm-client/), the
Qiskit serializer preserves these units. Applications submitting IQM JSON
directly must use the same format; the legacy `angle_t` and `phase_t` fields
expressed angles in turns.

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

## Pinning a calibration

Select the calibration before constructing the backend so that the Qiskit target
and submitted jobs use the same calibrated architecture and metrics:

```python
backend = IQMBackend(
    qc_alias="your-device",
    calibration_set_id="f0fb4be5-e913-4a04-8c94-18d1bd842def",
)
```

`backend.calibration_set_id` exposes the effective UUID, including when the
server default was resolved during initialization. Pass that UUID to a separate
execution client's `IQMBackend` constructor to preserve the compilation
calibration across processes. Keep the quantum computer selection the same. The
driver rejects invalid UUIDs, unavailable calibration sets, and a server
response that names a different set instead of silently using the default.

Explicit selection pins the session for its lifetime. Retrieving the result of a
calibration job does not replace its target or calibration; construct a new
backend with the returned UUID to adopt the new set. With no explicit selector,
legacy calibration-job refresh remains enabled: recreate the backend after such
a refresh to avoid reusing a cached target. The driver cannot determine which
calibration an arbitrary externally compiled circuit used; its producer and
consumer must carry and agree on the UUID.
