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

backend = IQMBackend(device_id="iqm.emerald.mock")
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
Pass `device_id` alongside any session overrides, such as
`IQMBackend(device_id="iqm.emerald.mock", token="…")`. Every backend opens an
independent device session.

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

## Calibrated qubits

`IQMBackend` exposes only qubits with both calibrated PRX and measurement
operations in its Qiskit target. Operations involving excluded qubits are also
removed from the target; computational resonators retain their native
operations. Opening a backend fails if no qubits have both required
calibrations.

Circuit qubit indices and `initial_layout` entries refer to this target. For
example, if QB2 is unavailable on a three-qubit device, target indices 0 and 1
refer to QB1 and QB3. `backend.physical_qubits` records this target-to-device
mapping. The IQM JSON serializer uses it to select physical site names,
preserving classical measurement destinations and circuit metadata. The
underlying QDMI device and generic MQT Core `QDMIBackend` still expose physical
site indices.

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

## IQM run-request options

Configure execution with IQM `CircuitJobDefinition` fields in
`run_request_options`:

```python
backend.set_options(run_request_options={"dd_mode": "enabled"})
job = backend.run(
    transpile(qc, backend),
    shots=128,
    run_request_options={"dd_mode": "enabled", "active_reset_cycles": 2},
)
```

The mapping on `run` replaces the backend default for that run and applies to
every circuit in the job. Use `None` or `{}` to use server defaults. Set `shots`
on `run` and select `calibration_set_id` when constructing the backend;
`circuits`, `shots`, and `calibration_set_id` are reserved request fields.

Values must be JSON-compatible and numbers must be finite. The IQM service
defines the supported fields and values; consult your server's
[IQM `PostJobsRequest` model](https://docs.iqm.tech/iqm-station-control-client/api/iqm.station_control.interface.models.circuit.PostJobsRequest.html).
For sampling, pass the mapping as
`backend.sampler(run_options={"run_request_options": options})`. For estimation,
configure it with `backend.set_options(...)` before creating the estimator. This
interface is available through `IQMBackend` and its bound primitives.

IQM's
[`heralding_mode="zeros"`](https://docs.iqm.tech/iqm-station-control-client/api/iqm.station_control.interface.models.circuit.HeraldingMode.html)
may discard shots that fail the initial-state check. `IQMBackend.run` rejects
this setting with `CircuitValidationError` before creating or submitting a job,
including when it is set as a backend default. Omit `heralding_mode` or use
`"none"` to retain all shots.

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

`backend.calibration_set_id` gives the effective UUID, including a resolved
server default. Share this UUID and the quantum computer identity with a
separate execution client, and pass the UUID to its `IQMBackend` constructor. An
externally compiled circuit does not carry its calibration UUID automatically.

Every backend keeps its calibration for its lifetime, including one resolved
from the server default. A calibration job returns a new UUID without changing
the existing backend. Construct a new backend with that UUID to compile and run
circuits against the new calibration.
