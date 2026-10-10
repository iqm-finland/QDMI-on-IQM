# IQM Slurm smoke test

This test runs a Qiskit workload against IQM's Emerald Resonance mock in
[MQT Core's Slurm cluster example](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm_cluster.html).
It requires a valid `IQM_TOKEN` and network access to
`https://resonance.iqm.tech`.

Use the MQT Core revision pinned in the Slurm workflow and build its Linux wheel
with `uv build --wheel --out-dir "$CORE_DIST" -Ccmake.define.DEPLOY=ON` from
that checkout. The wheel must match the host architecture and the cluster's
Python interpreter. The example deploys Slurm 25.11 or newer through rootful
Docker on a disposable Linux cgroup-v2 host.

```sh
PROVIDER_INSTALL_MODE=native uv run --no-project \
  "$CORE_SOURCE/test/slurm/run_integration.py" \
  --workload . --dist "$CORE_DIST" \
  --setup-script test/slurm/setup.sh \
  --compose-file test/slurm/compose.yml \
  --device-license iqm.emerald.mock \
  --qdmi-config-file /opt/provider-catalogue.json \
  -- python3 /workload/test/slurm/probe.py iqm.emerald.mock
```

Repeat with `PROVIDER_INSTALL_MODE=wheel`. Both modes install the Python adapter
and check the actual loaded library path. The workload submits eight Bell-state
shots through `IQMBackend(device=...)` in each mode. Jobs run as the
unprivileged `mqt-dev` user. Only the `iqm.emerald.mock` catalogue entry is
enabled.

The Compose overlay passes `IQM_TOKEN` to the login node for job submission and
to the controller for availability monitoring. Slurm exports the login node's
submission environment to the job. This smoke test uses one token for both
roles; production deployments should configure separate site-owned monitor
credentials. It does not forward `IQM_TOKENS_FILE`. Keep tokens out of command
arguments, build arguments, and logs. CI receives the repository's
`RESONANCE_API_KEY` as `IQM_TOKEN` and reports an explicit skip when that secret
is unavailable.

See [IQM on Slurm](../../docs/spank_plugin.md) for deployment.

For jobs from several vendors on the same cluster, use the
[MQT Core multi-vendor example](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm_cluster.html#multiple-device-implementations).
MQT Core and all device implementations share one Python environment. Each job
opens an explicit device ID through the driver, with the credentials that device
needs. All quantum access nodes share the `quantum` partition and can reach
every configured device. The cluster monitors each device before admitting its
jobs. `setup.sh [OUTPUT]` writes the catalogue to the chosen path.
