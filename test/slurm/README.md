# IQM Slurm smoke test

This test runs a Qiskit workload against IQM's Emerald Resonance mock in
[MQT Core's Docker Slurm cluster](https://github.com/munich-quantum-toolkit/core/tree/main/docker/slurm).
It requires a valid `IQM_TOKEN` and network access to
`https://resonance.iqm.tech`.

Use the MQT Core revision pinned in the Slurm workflow and build its Linux wheel
with `uv build --wheel --out-dir "$CORE_DIST" -Ccmake.define.DEPLOY=ON` from
that checkout. The wheel must match the Docker host architecture. The fixture
requires rootful Docker on a disposable Linux cgroup-v2 host and Slurm 25.11 or
newer.

```sh
PROVIDER_INSTALL_MODE=native uv run --no-project \
  "$CORE_SOURCE/test/slurm/run_integration.py" \
  --workload . --dist "$CORE_DIST" \
  --setup-script test/slurm/setup.sh \
  --compose-file test/slurm/compose.yml \
  --device-license iqm.emerald.mock \
  --qdmi-config-file /opt/provider-catalogue.json \
  -- sh -ec 'mqt-core-qdmi-check --device iqm.emerald.mock --timeout 30; exec python3 /workload/test/slurm/probe.py'
```

Repeat with `PROVIDER_INSTALL_MODE=wheel`. Both modes install the Python adapter
and check the actual loaded library path. The workload submits eight Bell-state
shots through `IQMBackend(device=...)` in each mode. Jobs run as an unprivileged
user. Only the `iqm.emerald.mock` catalogue entry is enabled.

The Compose overlay passes `IQM_TOKEN` from the host environment to the
controller at runtime; Slurm exports it to the job. It does not forward
`IQM_TOKENS_FILE`. Keep tokens out of command arguments, build arguments, and
logs. CI receives the repository's `RESONANCE_API_KEY` as `IQM_TOKEN` and
reports an explicit skip when that secret is unavailable.

See [IQM on Slurm](../../docs/spank_plugin.md) for deployment.
