---
file_format: mystnb
kernelspec:
  name: python3
language_info:
  name: python
mystnb:
  number_source_lines: true
---

# Using Qiskit to Run Quantum Workloads on IQM Hardware via QDMI-on-IQM

The [`iqm-qdmi` Python package](python_package.md) includes a wrapper for the
[IQM QDMI Device library](usage.md) that integrates it with Qiskit. This wrapper
is implemented in the {py:mod}`iqm.qdmi.qiskit` submodule and is based on the
open-source, MIT-licensed MQT Core library. To use the wrapper, make sure to
install the `iqm-qdmi` package with the `qiskit` extra:

This draft requires the shared compiler changes in
[MQT Core #2886](https://github.com/munich-quantum-toolkit/core/pull/2886)
and its PennyLane integration in
[Core #2767](https://github.com/munich-quantum-toolkit/core/pull/2767).
Use the exact Core source revision pinned by this checkout until those changes
are released:

```console
uv sync --extra qiskit
```

A published package must raise its minimum Core version to that release.

Then, the {py:class}`~iqm.qdmi.qiskit.IQMBackend` class can be imported from
{py:mod}`iqm.qdmi.qiskit` and used with Qiskit's transpilation and execution
interfaces.

```{code-cell} ipython3
from iqm.qdmi.qiskit import IQMBackend
from mqt.core.mlir import compile_program
from qiskit.circuit import QuantumCircuit

backend = IQMBackend(device_id="iqm.emerald.mock")
```

```{code-cell} ipython3
qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

native_qc = compile_program(qc, target=backend)
result = backend.run(native_qc, shots=128).result()
print(result.get_counts())
```

Compile against the selected backend before submission. `run()` validates and
serializes native instructions without compiling or routing them. Qiskit's
`transpile(qc, backend)` remains supported, including for MOVE circuits.

Select any stable ID from the
[installed catalogue](usage.md#using-the-device-with-mqt-core).
Pass `device_id` alongside any session overrides, such as
`IQMBackend(device_id="iqm.emerald.mock", token="…")`. Every backend opens an
independent device session. Pass an already-open handle with
`IQMBackend(device=...)` to reuse a device opened through MQT Core's driver; see
[IQM on Slurm](slurm.md).

`IQMBackend()` uses the configurable `iqm.default` connection. Explicit
arguments override environment defaults: `IQM_SERVER_URL`, `IQM_TOKEN`,
`IQM_TOKENS_FILE`, `IQM_QC_ID`, and `IQM_QUANTUM_COMPUTER`. `IQM_BASE_URL` and
`IQM_QC_ALIAS` are aliases, with canonical variables taking precedence. An
explicit quantum computer ID or alias suppresses both environment selectors.

Named presets use their configured endpoint and quantum computer; routing
environment variables cannot redirect them. Authentication uses the token or
token-file defaults. Explicit arguments and driver configuration can override
manifest values.

IQM JSON represents PRX rotation and phase angles in radians, using the `angle`
and `phase` fields. Like [IQM Client](https://docs.iqm.tech/iqm-client/), the
Qiskit serializer uses these units. Applications submitting IQM JSON directly
must use the same format. The device writes integer-valued PRX angles and phases
as floating-point JSON numbers for IQM's argument validator.

## Calibrated qubits

`IQMBackend` builds its Qiskit target from qubits with both PRX and measurement
calibrations, together with the device's computational resonators. At least one
calibrated qubit is required.

Circuit qubit indices and `initial_layout` entries are physical device site
indices. For example, if QB2 is unavailable on a three-qubit device, usable
indices remain 0 and 2 for QB1 and QB3. The target excludes operations involving
uncalibrated qubits without renumbering the remaining sites. The transpiled
circuit retains its Qiskit layout and addresses the same physical sites during
IQM JSON serialization.

## Measurement results

Ordered samples are included by default (`memory=True`). Use
`result.get_memory()` to access them, or pass `memory=False` to return counts
only.

Measurements must follow all gates. MQT Core serializes each measured qubit once
and keeps a result layout that reconstructs the circuit's classical bits. Unused
classical bits remain zero, repeated measurements retain their destinations, and
the final write wins when a classical bit is overwritten. Counts and memory
retain the Qiskit register names, boundaries, and bit order. Intermediate
measurements are rejected before submission.

MOVE circuits retain the existing resonator serializer until MQT Core supports
MOVE. This path requires each qubit to be measured at most once and every
classical bit exactly once. Calibrated site indices and resonators are preserved
on both paths.

## Circuit Metadata

Circuit names and `QuantumCircuit.metadata` remain in the local Qiskit result
header. Arbitrary metadata is not sent to the IQM service. The shared compiler's
IQM payload uses its entry-point name, `main`, independently of the circuit
name.

## Sampler and Estimator Primitives

{py:class}`~iqm.qdmi.qiskit.IQMBackend` provides small helpers (see
{py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler` and
{py:meth}`~iqm.qdmi.qiskit.IQMBackend.estimator`) for constructing
{py:class}`~qiskit.primitives.BackendSamplerV2` and
{py:class}`~qiskit.primitives.BackendEstimatorV2` primitives bound to the
backend instance.

```{code-cell} ipython3
sampler_job = backend.sampler().run([(native_qc,)], shots=128)
counts = sampler_job.result()[0].data["meas"].get_counts()
print(f"Counts: {counts}")
```

```{code-cell} ipython3
from qiskit.quantum_info import SparsePauliOp

native_qc.remove_final_measurements(inplace=True)
observable = SparsePauliOp("Z" * backend.num_qubits)

estimator_job = backend.estimator().run([(native_qc, observable)])
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
    compile_program(qc, target=backend),
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

Execution requires the requested number of shots. Omit `heralding_mode` or use
`"none"`. The shot-discarding
[`"zeros"` mode](https://docs.iqm.tech/iqm-station-control-client/api/iqm.station_control.interface.models.circuit.HeraldingMode.html)
raises `CircuitValidationError` in `IQMBackend.run` before submission.

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
