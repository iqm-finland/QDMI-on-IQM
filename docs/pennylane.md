# PennyLane

Install the optional adapter:

```console
uv pip install "iqm-qdmi[pennylane]"
```

Select a device from the
[installed catalogue](usage.md#using-the-device-with-mqt-core) and create a
QNode:

```python
import pennylane as qml

device = qml.device("iqm.qdmi", device_id="iqm.emerald.mock", wires=2)


@qml.qnode(device)
def bell():
    qml.Hadamard(0)
    qml.CNOT([0, 1])
    return qml.sample(wires=[0, 1])


samples = qml.set_shots(bell, shots=128)()
```

{py:class}`iqm.qdmi.pennylane.IQMDevice` uses MQT Core for compilation,
submission, and measurement results, including routing through computational
resonators. See the
[MQT Core PennyLane guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/pennylane_device.html)
for supported operations, measurements, and Catalyst integration.

Authentication defaults to `IQM_TOKEN` or `IQM_TOKENS_FILE`. Pass `token`,
`tokens_file`, or `calibration_set_id` to configure the session explicitly.
`iqm.default` also reads the endpoint and quantum computer environment variables
listed in the [Qiskit guide](qiskit.md); named devices use their manifest
settings.

Use `job_parameters={"custom1": json.dumps(options)}` for IQM run-request
fields. The native device validates these fields and rejects shot-discarding
heralding; see [IQM run-request options](qiskit.md#iqm-run-request-options).
