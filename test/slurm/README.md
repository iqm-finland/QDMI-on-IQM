# IQM Slurm smoke test

This test runs an eight-shot Qiskit Bell circuit on IQM's Emerald Resonance mock
in
[MQT Core's shared Slurm cluster](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html).

Export a valid `IQM_TOKEN` before running the test. The mock service requires
network access to `https://resonance.iqm.tech`.

Check out the MQT Core revision pinned in the Slurm workflow and set
`CORE_SOURCE` to that checkout. Follow the
[cluster setup](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html#start-the-cluster-and-log-in)
for Docker Engine on Linux or Docker Desktop on macOS, then run from this
repository:

```bash
uv run --no-project "$CORE_SOURCE/test/slurm/run_integration.py" \
  --workload . --package /workload \
  --compose-file test/slurm/compose.yml \
  --device-license iqm.emerald.mock \
  -- python3 /workload/test/slurm/workload.py
```

Docker builds and installs the device wheel in the cluster's shared Python
environment. The driver discovers its installed catalogue. The workload opens
`iqm.emerald.mock`, checks that the wheel's library is loaded, and verifies the
shot counts. Jobs run as the unprivileged `mqt-dev` user.

The Compose overlay passes credentials to the login node for submission and to
the controller for availability monitoring. Slurm exports the submission
environment to the job. This test uses one identity for both roles; production
deployments should configure separate site-owned monitor credentials.

See [IQM on Slurm](../../docs/slurm.md) for deployment and the shared
[example with multiple device implementations](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/slurm.html#multiple-device-implementations)
for workloads from several vendors on the same cluster.

## Running CI in a fork

Slurm tests are skipped for pull requests from another repository. To run the
credentialed tests in your own fork, enable GitHub Actions and configure
`RESONANCE_API_KEY` (passed to the workload as `IQM_TOKEN`) as repository
secrets. Dispatch the CI workflow on your branch, or push to the fork's `main`
branch. These secrets are available to runs in your fork; they are not passed to
the upstream pull-request workflow.
