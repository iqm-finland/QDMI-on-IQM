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

Save this workload as `bell.py`. It uses the allocated license as its catalogue
ID, assuming one local license with a count of one (`ID` or `ID:1`):

```python
import os

from mqt.core.qdmi import builtin_driver as qdmi_driver
from qiskit import QuantumCircuit, transpile
from iqm.qdmi.qiskit import IQMBackend

device_id = os.environ["SLURM_JOB_LICENSES"].removesuffix(":1")
backend = IQMBackend(device=qdmi_driver.open_device(device_id))
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
result = backend.run(transpile(circuit, backend), shots=100).result()
print(result.get_counts())
```

Request the Emerald Resonance mock's license on the site's quantum partition:

```console
srun --partition=quantum --licenses=iqm.emerald.mock python bell.py
```

Applications that use several devices open their catalogue IDs explicitly and
request the corresponding licenses. IQM and other device credentials can coexist
in one job environment. Keep tokens out of command arguments, Slurm
configuration, and committed scripts.

The
[offloader](python_package.md#programmatic-offloading-with-the-offloader-module)
also submits IQM sampler and estimator jobs through `srun`. IQM supports IQM
JSON and QIR. MQT Core's PennyLane adapter requires OpenQASM; use the IQM Qiskit
adapter for these workloads.

## Prepare the cluster

Install the [Python package](python_package.md) with its Qiskit adapter in the
workload environment on login and quantum access nodes:

```console
uv pip install 'iqm-qdmi[qiskit]'
```

The driver discovers the package's catalogue, including `iqm.emerald`,
`iqm.garnet`, and their `.mock` variants. Native installations need a readable
catalogue and library on every quantum access node; retain the Python package
for the adapter. All nodes in the `quantum` partition can share one environment
and reach every configured device.

Register each device ID as a Slurm license. For example,
`Licenses=iqm.emerald.mock:1` permits one allocation at a time for the Emerald
Resonance mock. A cluster availability monitor can reserve this license while
the device is unavailable; give it separate site-owned IQM credentials and
network access to Resonance. See the
[Slurm deployment guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
for scheduler configuration and monitoring.

Administrators can put shared non-secret settings in `/etc/mqt-core/qdmi.json`
and expose the workload environment through site defaults or a software module.
See [device configuration](usage.md#session-configuration) for quantum computer
IDs, aliases, and their precedence.

## Exercise the integration

[MQT Core's cluster playground](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm_cluster.html)
is a concrete deployment with a shared quantum access partition and several
device implementations. Its build instructions use current source revisions for
the unreleased QDMI 1.4 and MQT Core 4.1 interfaces.

The
[Slurm smoke test](https://github.com/iqm-finland/QDMI-on-IQM/tree/main/test/slurm)
uses that playground to run an authenticated eight-shot Qiskit job on the
Emerald Resonance mock for native and wheel installations. Its README describes
the required credentials and commands.
