# IQM on Slurm

Use
[MQT Core's Slurm integration](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
to schedule QDMI workloads. That guide covers cluster setup, license counts,
optional site defaults, and the Docker cluster. This page supplies the IQM
runtime, credentials, and workload.

## Install and configure the device

Install the [Python package](python_package.md) in the environment available on
each compute node:

```console
uv pip install 'iqm-qdmi[qiskit]'
```

The package includes the device library and its catalogue. Locate the catalogue
for native commands such as `mqt-core-qdmi-check`:

```bash
export MQT_CORE_QDMI_CONFIG_FILE=$(python -c 'from iqm.qdmi import IQM_QDMI_LIBRARY_PATH; print(IQM_QDMI_LIBRARY_PATH.parent / "iqm-qdmi-device.qdmi.json")')
```

For a native installation, use the catalogue installed beside the library
instead. Keep the Python package available for the Qiskit adapter.

The installed catalogue includes `iqm.emerald`, `iqm.garnet`, and their `.mock`
variants. Register the selected ID as a Slurm license. For example,
`Licenses=iqm.emerald.mock:1` admits one allocation at a time for the Resonance
mock endpoint. It still requires IQM credentials and network access.

Use the [device configuration](usage.md#session-configuration) to pin a
site-specific quantum computer by its actual QC ID when needed. An inherited
`IQM_QC_ID` takes precedence over an alias; unset it when using an alias-based
catalogue entry.

## Credentials

Set `IQM_TOKENS_FILE` to a credential file readable by the job user on each
compute node. Keep tokens out of Slurm configuration and scripts committed to
source control. See [authentication](usage.md#authentication-methods) for
supported credential sources and token renewal.

Slurm exports the submission environment. Administrators can supply default
catalogue and credential-file paths through MQT Core's optional SPANK module;
the job environment overrides those defaults.

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

After activating the workload environment and setting the catalogue path:

```bash
export IQM_TOKENS_FILE=/shared/iqm/tokens.json
unset IQM_QC_ID
srun --licenses=iqm.emerald.mock python bell.py
```

An optional availability probe can run inside the allocation before the
workload, after environment setup. Follow MQT Core's job-script example. The
[offloader](python_package.md#programmatic-offloading-with-the-offloader-module)
also submits IQM sampler and estimator jobs through `srun`.

IQM supports IQM JSON and QIR. MQT Core's PennyLane adapter requires OpenQASM;
use the IQM Qiskit adapter for these workloads.

## Test locally

The
[Slurm smoke test](https://github.com/iqm-finland/QDMI-on-IQM/tree/main/test/slurm)
uses a local HTTP mock and MQT Core's Docker cluster. It checks native and wheel
installations without contacting IQM services. Follow its README to run it.
