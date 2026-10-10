# IQM on Slurm

[MQT Core's Slurm integration](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
provides the cluster configuration, device licenses, and availability monitor.
Install IQM alongside the other QDMI device implementations in the same workload
environment on every compute node. MQT Core's driver opens the device selected
by the job's license; the IQM implementation handles authentication and quantum
execution.

## Configure IQM access

Install the [Python package](python_package.md) with its Qiskit adapter:

```console
uv pip install 'iqm-qdmi[qiskit]'
```

MQT Core discovers the installed device catalogue from the Python package. For
the unreleased QDMI 1.4 and MQT Core 4.1 interfaces, build the repositories'
current source revisions together as shown in the
[shared cluster example](https://github.com/munich-quantum-toolkit/core/tree/main/examples/slurm).
Native installations need a readable catalogue and library on each compute node;
retain the Python package for the Qiskit adapter.

The catalogue includes `iqm.emerald`, `iqm.garnet`, and their `.mock` variants.
Register the selected ID as a Slurm license: `Licenses=iqm.emerald.mock:1`
permits one allocation at a time for the Emerald Resonance mock. The cluster's
availability monitor reserves the license while this device is unavailable. It
needs IQM credentials and network access, just as workloads do.

Set `IQM_TOKENS_FILE` to a credential file readable by the job user on each
compute node. See [authentication](usage.md#authentication-methods) for other
credential sources and token renewal. Keep tokens out of Slurm configuration and
committed scripts. Slurm exports the submission environment; AWS credentials and
IQM credentials can coexist in the same job environment.

Use the [device configuration](usage.md#session-configuration) to pin a quantum
computer by its actual QC ID when needed. An inherited `IQM_QC_ID` takes
precedence over an alias; unset it when using an alias-based catalogue entry.

## Run a Qiskit job

Save this workload as `bell.py`:

```python
from mqt.core.qdmi import slurm
from qiskit import QuantumCircuit, transpile
from iqm.qdmi.qiskit import IQMBackend

backend = IQMBackend(device=slurm.open_device_from_license())
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
result = backend.run(transpile(circuit, backend), shots=100).result()
print(result.get_counts())
```

Activate the shared workload environment and submit the job:

```bash
export IQM_TOKENS_FILE=/shared/iqm/tokens.json
unset IQM_QC_ID
srun --licenses=iqm.emerald.mock python bell.py
```

The
[offloader](python_package.md#programmatic-offloading-with-the-offloader-module)
also submits IQM sampler and estimator jobs through `srun`. IQM supports IQM
JSON and QIR. MQT Core's PennyLane adapter requires OpenQASM; use the IQM Qiskit
adapter for these workloads.

## Exercise the integration

The
[Slurm smoke test](https://github.com/iqm-finland/QDMI-on-IQM/tree/main/test/slurm)
runs an eight-shot Qiskit job on the Emerald Resonance mock for both native and
wheel installations. It uses MQT Core's shared cluster example, with real Slurm
scheduling and IQM authentication. Its README describes the credentials and
commands.
