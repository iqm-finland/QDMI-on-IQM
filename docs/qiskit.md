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

## IQM execution options

Configure defaults with `backend.set_options(...)`, or override them for one
`backend.run(...)` call. `None` leaves the native IQM default unchanged. Unknown
names and invalid values raise `CircuitValidationError` before submission.

```python
backend.set_options(heralding_mode="zeros", active_reset_cycles=2)
job = backend.run(transpiled_qc, shots=128, dd_mode="enabled")
sampler = backend.sampler(run_options={"heralding_mode": "none"})
estimator = backend.estimator()  # Uses the backend's execution-option defaults.
```

| Option                         | Accepted values                                | Native default |
| ------------------------------ | ---------------------------------------------- | -------------- |
| `heralding_mode`               | `"none"`, `"zeros"`                            | `"none"`       |
| `move_gate_validation`         | `"strict"`, `"allow_prx"`, `"none"`            | `"strict"`     |
| `move_gate_frame_tracking`     | `"full"`, `"no_detuning_correction"`, `"none"` | `"full"`       |
| `dd_mode`                      | `"disabled"`, `"enabled"`                      | `"disabled"`   |
| `qubit_mapping`                | Mapping of logical to physical qubit names     | Omitted        |
| `max_circuit_duration_over_t2` | Finite positive number                         | Omitted        |
| `active_reset_cycles`          | Nonnegative integer representable as `size_t`  | Omitted        |
| `dd_strategy`                  | Finite JSON-compatible dictionary              | Omitted        |

Qubit mapping names must be nonempty strings without commas, colons, or NUL
characters. They refer to names in the submitted program; the IQM JSON
serializer already uses physical device site names. The IQM server validates the
contents of `dd_strategy` against its supported strategy schema.

This feature requires MQT Core's `QDMIBackend._job_parameters` extension hook.
With older MQT Core versions, ordinary runs remain supported and the new options
are rejected. Install a version containing the companion MQT Core change to use
them; the package dependency will be raised when that version is released.
Native estimators use backend defaults because Qiskit's estimator does not offer
a `run_options` field. These settings do not add CLI/offloader option
forwarding.

## CLI Scripts

The package also exposes the `iqm-sampler` and `iqm-estimator` CLI scripts for
executing serialized circuits directly from the shell. For more details on these
utilities and their usage, see the [Python Package Guide](python_package.md).
