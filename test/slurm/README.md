# IQM Slurm smoke test

This test runs a Qiskit workload against a local IQM HTTP mock in
[MQT Core's Docker Slurm cluster](https://github.com/munich-quantum-toolkit/core/tree/main/docker/slurm).
It requires rootful Docker on a disposable Linux cgroup-v2 host.

Use the MQT Core revision pinned in the Slurm workflow and build its Linux wheel
with `uv build --wheel --out-dir "$CORE_DIST"` from that checkout. The wheel
must match the Docker host architecture. The fixture requires rootful Docker on
a disposable Linux cgroup-v2 host and Slurm 25.11 or newer.

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

Repeat with `PROVIDER_INSTALL_MODE=wheel`. Both modes install the Python adapter
and check the actual loaded library path. The workload submits eight Bell-state
shots through `IQMBackend(device=...)`, once with explicit job configuration and
once with site defaults. Jobs run as an unprivileged user.

The local mock accepts only fixture credentials. The setup script selects it and
disables installed live presets, so this test cannot submit to IQM services. See
[IQM on Slurm](../../docs/spank_plugin.md) for deployment.
