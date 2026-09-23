# IQM workload for the shared Core Slurm fixture

MQT Core owns the Docker image, Slurm services, configuration, SPANK build,
transport assertions, and cleanup. This directory supplies the IQM mock, site
catalogue, and Qiskit circuit. No live IQM access is needed.

Use a Core checkout containing the shared fixture and a directory containing its
released Linux `mqt_core-4.0.0` wheel for the Docker host architecture. The
fixture requires privileged Docker with cgroup v2 and Slurm 25.11 or newer.

```sh
PROVIDER_INSTALL_MODE=native uv run --no-project \
  "$CORE_SOURCE/test/slurm/run_integration.py" \
  --workload . --dist "$CORE_DIST" \
  --setup-script test/slurm/setup.sh \
  --compose-file test/slurm/compose.yml \
  --device-license iqm.fixture.emerald \
  --qdmi-config-file /opt/provider-catalogue.json \
  --reference IQM_TOKENS_FILE=/opt/iqm-fixture-tokens.json \
  -- python3 /workload/test/slurm/probe.py
```

Repeat with `PROVIDER_INSTALL_MODE=wheel`. Both modes install the Python
adapter. Native mode selects the separately installed Runtime; wheel mode
selects the bundled library. The site catalogue pins `custom1` to the mock QC ID
so inherited aliases cannot redirect the licensed workload. The probe checks the
loaded library path, opens the license-selected handle, and submits and
retrieves eight Bell-state shots through `IQMBackend(device=...)`. It also
checks the existing PennyLane unsupported-format error: IQM's JSON/QIR formats
do not have a Core 4.0 PennyLane serializer. Core runs the same workload as a
non-root user with explicit environment setup and with shared SPANK injection,
then runs its generic transport checks. The mock requires the unsigned fixture
credential from the generated token file; the catalogue leaves `auth-file` unset
to exercise `IQM_TOKENS_FILE`.

See [IQM on Slurm](../../docs/spank_plugin.md) for deployment and migration.
