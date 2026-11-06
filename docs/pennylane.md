# PennyLane

The {py:class}`iqm.qdmi.pennylane.IQMDevice` adapter uses MQT Core for
compilation, submission, and measurement results.

## Install

```console
uv pip install "iqm-qdmi[pennylane]"
```

## Configure a device

Select a device from the
[installed catalogue](usage.md#using-the-device-with-mqt-core):

```python
import pennylane as qp

device = qp.device("iqm.qdmi", device_id="iqm.emerald.mock", wires=2)
```

Authentication defaults to `IQM_TOKEN` or `IQM_TOKENS_FILE`. Pass `token`,
`tokens_file`, or `calibration_set_id` to configure the session explicitly.
`iqm.default` also reads the endpoint and quantum computer environment variables
listed in the [Qiskit guide](qiskit.md); named devices use their manifest
settings.

Use `job_parameters={"custom1": json.dumps(options)}` for IQM run-request
fields. The native device validates these fields and rejects shot-discarding
heralding; see [IQM run-request options](qiskit.md#iqm-run-request-options).

## Run a QNode

```python
@qp.qnode(device)
def bell():
    qp.Hadamard(0)
    qp.CNOT([0, 1])
    return qp.sample(wires=[0, 1])


samples = qp.set_shots(bell, shots=1000)()
```

## Supported scope

MQT Core compiles circuits for the selected device, including routing through
computational resonators. Execution uses finite shots. See the
[MQT Core PennyLane guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/pennylane_device.html)
for supported operations, measurements, and optional Catalyst integration.
