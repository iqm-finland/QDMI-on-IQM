---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Using Qiskit to Run Quantum Workloads on IQM Hardware via QDMI-on-IQM

The [`iqm-qdmi` Python package](python_package.md) includes a wrapper for the [IQM QDMI Device library](usage.md) that integrates it with Qiskit.
This wrapper is implemented in the {py:mod}`iqm.qdmi.qiskit` submodule and is based on the open-source, MIT-licensed MQT Core library.
To use the wrapper, make sure to install the `iqm-qdmi` package with the `qiskit` extra:

```console
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

If no explicit arguments are provided, the wrapper resolves `IQM_BASE_URL`, `IQM_TOKEN`, `IQM_TOKENS_FILE`, `IQM_QC_ID`, and `IQM_QC_ALIAS` from the environment. Alternatively, pass any of those values directly to `IQMBackend(...)`.

## Sampler and Estimator Primitives

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

## CLI Scripts

The package also exposes the `iqm-sampler` and `iqm-estimator` CLI scripts for executing serialized circuits directly from the shell. For more details on these utilities and their usage, see the [Python Package Guide](python_package.md).
