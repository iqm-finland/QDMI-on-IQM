# IQM on Slurm

A Slurm job requests a device license and uses the QDMI Client Interface to
access that device. Slurm schedules the allocation; the IQM device
implementation handles authentication and quantum execution. The same job
environment can include device implementations from several vendors.

## Submit a Qiskit job

Activate your site's workload environment and set your IQM credentials before
submission. For example, `IQM_TOKENS_FILE` can name a credential file readable
at the same path on each quantum access node. Slurm exports the submission
environment to the job. See [authentication](usage.md#authentication-methods)
for token renewal and other credential sources.

Save this workload as `bell.py`. This example requests and opens the fixed
catalogue ID `iqm.emerald.mock`:

```{code-block} python
:caption: bell.py

from qiskit import QuantumCircuit
from iqm.qdmi.qiskit import IQMBackend

backend = IQMBackend(device_id="iqm.emerald.mock")
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
result = backend.run(backend.compile(circuit), shots=100).result()
print(result.get_counts())
```

Request the Emerald Resonance mock's license on the site's quantum partition:

```bash
srun --partition=quantum --licenses=iqm.emerald.mock:1 python bell.py
```

For requests with alternative devices, use the granted resource from Slurm's
allocation record to select the QDMI device. See
[device selection in MQSC Orchestrator](https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md#from-allocation-to-device).
IQM and other device credentials can coexist in one job environment. Keep tokens
out of command arguments, Slurm configuration, and committed scripts.

The
[offloader](python_package.md#programmatic-offloading-with-the-offloader-module)
also submits IQM sampler and estimator jobs through `srun`. IQM supports IQM
JSON and QIR. The
[MQT Core PennyLane device](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/pennylane_device.html)
also compiles to IQM JSON and reconstructs the requested measurement results.

## Prepare the cluster

Install the [Python package](python_package.md) with its Qiskit adapter in the
workload environment on login and quantum access nodes:

```bash
uv pip install 'iqm-qdmi[qiskit]'
```

The driver discovers the package's catalogue, including `iqm.emerald`,
`iqm.garnet`, and their `.mock` variants. Native installations need a readable
catalogue and library on every quantum access node; retain the Python package
for the adapter. Quantum access nodes need outbound access to the IQM service.

[MQSC Orchestrator's deployment guide](https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md#existing-clusters)
covers scheduler setup and device registration. Slurm controls local
allocations; IQM still controls remote access and scheduling.

Administrators can put shared non-secret settings in `/etc/mqt-core/qdmi.json`
and expose the workload environment through site defaults or a software module.
See [device configuration](usage.md#session-configuration) for quantum computer
IDs, aliases, and their precedence.
