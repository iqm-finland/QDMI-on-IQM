# IQM on Slurm

[MQT Core's shared Slurm example](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
configures device licenses and availability monitoring. Cluster administrators
install IQM alongside the other QDMI device implementations in one workload
environment on login and quantum access nodes. Applications open a catalogue ID
through MQT Core's driver; the IQM implementation handles authentication and
quantum execution.

The shared `quantum` partition contains interchangeable quantum access nodes.
Each node can reach every configured device; the catalogue ID selects the device
independently of the node running the job.

## Configure IQM access

Install the [Python package](python_package.md) with its Qiskit adapter:

```console
uv pip install 'iqm-qdmi[qiskit]'
```

MQT Core discovers the installed device catalogue from the Python package. For
the unreleased QDMI 1.4 and MQT Core 4.1 interfaces, build the repositories'
current source revisions together as shown in the
[shared cluster example](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm_cluster.html).
Native installations need a readable catalogue and library on each compute node;
retain the Python package for the Qiskit adapter. Administrators can put shared
non-secret settings in `/etc/mqt-core/qdmi.json`, which MQT Core reads
automatically, and make the workload environment available through the site
defaults or a software module.

The catalogue includes `iqm.emerald`, `iqm.garnet`, and their `.mock` variants.
Register the selected ID as a Slurm license: `Licenses=iqm.emerald.mock:1`
permits one allocation at a time for the Emerald Resonance mock. The cluster's
availability monitor reserves the license while this device is unavailable. It
uses separate site-owned IQM credentials and needs network access to Resonance.

Select your IQM credentials in the submission environment. For example, set
`IQM_TOKENS_FILE` to your credential file, readable at the same path on each
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
from mqt.core.qdmi import builtin_driver
from qiskit import QuantumCircuit, transpile
from iqm.qdmi.qiskit import IQMBackend

backend = IQMBackend(device=builtin_driver.open_device("iqm.emerald.mock"))
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
result = backend.run(transpile(circuit, backend), shots=100).result()
print(result.get_counts())
```

With the site environment and your credentials available, submit the job to the
site's quantum partition (`quantum` in the shared cluster example). Request the
license matching the device ID opened by the application:

```console
srun --partition=quantum --licenses=iqm.emerald.mock python bell.py
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
