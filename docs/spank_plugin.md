# IQM on Slurm

Use the
[MQT Core Slurm guide](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html)
for static licenses, shared optional SPANK injection, scheduler operations, and
the common Dockerized test setup. This page covers the IQM runtime, site
catalogue, credentials, and adapter. Shared injection requires Slurm 25.11 or
newer. Deploy it from a released Core version before replacing the provider
plugin.

## Choose the provider runtime

A Python installation includes the IQM native runtime:

```console
uv venv /opt/iqm
uv pip install --python /opt/iqm/bin/python 'iqm-qdmi[qiskit]'
/opt/iqm/bin/python -c 'from iqm.qdmi import IQM_QDMI_LIBRARY_PATH; print(IQM_QDMI_LIBRARY_PATH)'
```

Use the printed library path in the site catalogue below. The Python environment
must be available at the same path on compute nodes. A separate system runtime
is unnecessary for this workflow.

Sites that manage native libraries independently can instead install the Runtime
component:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_QDMI_TESTS=OFF
cmake --build build --target iqm-qdmi-device --parallel 2
sudo cmake --install build --component iqm-qdmi-device_Runtime
```

Set the catalogue's library to the installed system library, normally
`/usr/local/lib/libiqm-qdmi-device.so`. Python adapters still come from the
wheel, but Core opens the system library explicitly named in the catalogue. The
Development component is only needed for C++ consumers.

## Define a concrete site catalogue

Create a trusted `qdmi.json` with one stable ID per schedulable quantum
computer. Replace the example endpoint, library, and QC ID with the site's
actual values:

```json
{
  "schema-version": 1,
  "qdmi": {
    "devices": [
      {
        "id": "iqm.site.emerald",
        "library": "/path/to/libiqm-qdmi-device.so",
        "prefix": "IQM",
        "enabled": true,
        "session": {
          "base-url": "https://resonance.iqm.tech",
          "custom1": "SITE_QUANTUM_COMPUTER_ID"
        }
      }
    ]
  }
}
```

Pin the QC ID using `custom1`. An alias alone does not protect the selection
from an inherited `IQM_QC_ID`. The generic `iqm.default` remains available for
direct configurable use, but is not a concrete Slurm resource. Match the Slurm
license exactly to the catalogue ID.

Set `MQT_CORE_QDMI_CONFIG_FILE` to the site catalogue before opening a device,
or configure that path as the shared injector's `qdmi_config_file` default.

## Credentials

Keep credentials in files readable by the job user. The provider continues to
resolve `IQM_TOKEN` and `IQM_TOKENS_FILE` through its existing authentication
logic. Do not put inline tokens in plugstack configuration or command options.
Leave `session.auth-file` unset in the catalogue when the job must select its
authentication file through the environment.

A shared-injection reference such as
`reference=IQM_TOKENS_FILE:iqm.site.emerald:/etc/iqm/tokens.json` supplies an
administrator default. An allowed explicit override is
`--qdmi-ref-IQM_TOKENS_FILE=/path/to/tokens.json`. Core documents reference
precedence. The injector carries the path; the IQM provider reads and validates
the credentials in the application process.

## Run a Qiskit job

Pass the licensed handle directly to `IQMBackend` so its IQM serialization and
MOVE support remain available:

```python
from iqm.qdmi.qiskit import IQMBackend
from mqt.core.qdmi import slurm
from qiskit import QuantumCircuit, transpile

backend = IQMBackend(device=slurm.open_device_from_license())
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
circuit = transpile(circuit, backend)
print(backend.run(circuit, shots=100).result().get_counts())
```

Without injection, export the catalogue and token-file reference:

```console
export MQT_CORE_QDMI_CONFIG_FILE=/etc/mqt-core/qdmi.json
export IQM_TOKENS_FILE=/path/to/tokens.json
srun --licenses=iqm.site.emerald /opt/iqm/bin/python bell.py
```

With shared injection configured, use the same workload and an allowed reference
override:

```console
srun --licenses=iqm.site.emerald --qdmi-ref-IQM_TOKENS_FILE=/path/to/tokens.json /opt/iqm/bin/python bell.py
```

`device=` cannot be combined with `base_url`, `token`, `tokens_file`, `qc_id`,
or `qc_alias`: an already-open handle has fixed session settings.

## PennyLane compatibility

IQM advertises IQM JSON and QIR, while Core 4.0's PennyLane adapter accepts
OpenQASM 2 or 3. Passing the licensed IQM handle to `QDMIDevice` currently
raises `PennyLaneUnsupportedFormatError`. The shared Slurm setup does not add a
program serializer; use the Qiskit workflow above.

## Migrate the provider plugin

Validate the shared Core setup before replacing the working deployment. Remove
the old `iqm-spank-plugin.so` directive and use Core's shared module. The
`BUILD_IQM_SPANK` option and provider plugin install component are removed. Do
not load the old and new modules together.

| Previous configuration                             | Migration                                                                           |
| -------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `iqm_qc_<alias>` licenses and `iqm_license_prefix` | Exact concrete site catalogue IDs                                                   |
| `iqm_base_url` / `--iqm-base-url`                  | Catalogue `session.base-url`                                                        |
| `iqm_qc_id` / `--iqm-qc-id`                        | Catalogue `session.custom1`                                                         |
| `iqm_qc_alias` / `--iqm-qc-alias`                  | Resolve the intended QC ID and pin it in the catalogue                              |
| `iqm_tokens_file` / `--iqm-tokens-file`            | Shared `IQM_TOKENS_FILE` reference                                                  |
| `iqm_log_level` / `--iqm-log-level`                | Submitted `IQM_LOG_LEVEL` environment                                               |
| `partitions` / `iqm_require_license`               | Explicit shared-injection license applicability and Core's static-license connector |
| `iqm_validation_timeout`                           | Removed with provider validation in Slurm hooks                                     |

Shared injection does not contact IQM or reject `/bin/true` because IQM is
unavailable. The application's Core device open performs authentication and
status checking. The optional generic Core launch checker can be enabled
separately once available; there is no retained provider validation mode.

## Validate the migration

The provider's local HTTP fixture and workload live in `test/slurm`. Core's
shared runner owns scheduler installation, startup, admission, injection, and
teardown. See `test/slurm/README.md` for native and wheel commands. Tests use
local fixtures without IQM credentials or hardware.
