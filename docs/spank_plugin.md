# Slurm SPANK Plugin

The Slurm SPANK plugin for QDMI-on-IQM simplifies running quantum jobs on
clusters by automatically propagating `IQM_*` environment variables to job
steps. This avoids manual `export` statements in job scripts and enables
administrators to configure global defaults and partition-gated access.

---

## For Users

The plugin registers `--iqm-*` command-line options for standard Slurm
submission tools (`srun`, `sbatch`, `salloc`). When these options are provided,
the plugin translates them into the corresponding environment variables for the
job tasks.

### Supported Options

| Option              | Environment Variable | Description                                           |
| :------------------ | :------------------- | :---------------------------------------------------- |
| `--iqm-base-url`    | `IQM_BASE_URL`       | The endpoint URL of the IQM service.                  |
| `--iqm-tokens-file` | `IQM_TOKENS_FILE`    | Path to the file containing your access tokens.       |
| `--iqm-qc-id`       | `IQM_QC_ID`          | The unique identifier of the target quantum computer. |
| `--iqm-qc-alias`    | `IQM_QC_ALIAS`       | The alias of the target quantum computer.             |

### Credential Security

Direct token passing is intentionally unsupported on the command line. Slurm
command arguments may be captured in shell history, process listings, scheduler
logs, or accounting records.

To run authenticated jobs:

1. Save your tokens to a secure file.
2. Pass the path to this file using the `--iqm-tokens-file` option.
3. Ensure the token file is readable on the compute nodes where the tasks
   execute.

---

## Example

The {py:class}`~iqm.qdmi.qiskit.IQMBackend` class automatically resolves
configuration values from the environment variables injected by the plugin.

**`bell_state.py`**

```python
from iqm.qdmi.qiskit import IQMBackend
from qiskit import QuantumCircuit, transpile

# Initialize the backend (resolves URL and auth from Slurm environment)
backend = IQMBackend()
print(f"Connected to: {backend.name}")

# Create a simple Bell state circuit
qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

# Transpile and execute
transpiled_qc = transpile(qc, backend)
job = backend.run(transpiled_qc, shots=100)
print(f"Job ID: {job.job_id()}")

# Retrieve results
result = job.result()
counts = result.get_counts()
print(f"Counts: {counts}")
```

To run this job:

```bash
srun --partition=quantum --iqm-qc-alias=emerald python bell_state.py
```

### Limiting Concurrent Access to a QC

If your administrator has configured a Slurm license for the target QC (see
[Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses)
below), request it alongside `--iqm-qc-alias` using Slurm's native
`--licenses`/`-L` option:

```bash
srun --partition=quantum --iqm-qc-alias=emerald --licenses=iqm_qc_emerald:1 python bell_state.py
```

This matters most for **on-premise QCs**: like Resonance, an on-premise setup
still runs its own internal queue, but it typically fronts single-tenant
hardware, so uncontrolled Slurm-side concurrency puts unnecessary pressure on
that queue. Requesting the license lets Slurm regulate that pressure itself,
ahead of the QC's own queue. If you omit `--licenses`, the default policy is a
silent no-op. If the administrator has set `iqm_require_license=1`, the plugin
instead fails your job step at launch — after it has already been allocated, not
at submission time.

### Executing via CLI Scripts

Alternatively, if you already have serialized circuits (in QPY format), you can
execute them directly on the cluster using the packaged CLI scripts (see the
[Sampler and Estimator CLI Utilities](python_package.md#sampler-and-estimator-cli-utilities)
documentation for details) without writing any custom Python code:

```bash
# Run a serialized circuit using the sampler CLI
srun --partition=quantum --iqm-qc-alias=emerald iqm-sampler bell.qpy --shots 100

# Estimate parameters using the estimator CLI
srun --partition=quantum --iqm-qc-alias=emerald iqm-estimator ansatz.qpy observable.pkl --maxiter 10
```

---

## For HPC Administrators

The SPANK plugin is a lightweight C++ module that intercepts job launches to
parse options and inject environment variables. It does not implement scheduler
policy or handle backend-side queue management.

For every active job step, the plugin initializes one IQM QDMI session on each
allocated node after Slurm drops privileges. This verifies the endpoint,
credentials, selected quantum computer, and architecture before any task starts
on that node. The result is cached per node for the step, so multi-task launches
on one node do not repeat the backend requests or routine task diagnostics. An
N-node job therefore performs N validation sessions. Validation uses the exact
SPANK job environment; daemon-only `slurmd` or `slurmstepd` IQM variables are
ignored. Every validation request has a non-optional 30-second default timeout.

### Compatibility and Requirements

- **Slurm Version**: Slurm 20.02 or newer. The optional Slurm license alignment
  check (see
  [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses))
  additionally requires Slurm 23.02 or newer, since it relies on the
  `SLURM_JOB_LICENSES` job environment variable; on older Slurm versions the
  default policy is a silent no-op, while `iqm_require_license=1` fails closed.
- **C++ Compiler**: C++20 standard library support (GCC 13+ or Clang 16+).
- **Compilation Constraint**: SPANK plugins are tied to the Slurm daemon ABI.
  You must compile the plugin against the target cluster's Slurm header files
  (`slurm/spank.h`) and rebuild the plugin after any major/minor Slurm upgrades.

### Installation

To compile and install the plugin from the repository root:

```bash
cmake -S . -B build-spank -DBUILD_IQM_SPANK=ON
cmake --build build-spank --target iqm-spank-plugin --parallel
sudo cmake --install build-spank --component iqm-spank-plugin
```

This installs the compiled `.so` file to the Slurm plugin directory and places
the template configuration in `plugstack.conf.d/`. The IQM QDMI implementation
used for launch-time validation is linked directly into the plugin.

Deploy the plugin on login/submit nodes (for `srun`/`sbatch` command line
parsing) and on compute nodes running `slurmd`/`slurmstepd`. Controller-only
nodes do not require the plugin.

### Configuration

Configure the plugin in `plugstack.conf`. Global defaults defined here can be
overridden by users at submission time.

**`/etc/slurm/plugstack.conf.d/iqm-qdmi.conf`**

The whole directive must be a single line; plugstack.conf does not support line
continuation.

```text
required /usr/lib/slurm/iqm-spank-plugin.so iqm_base_url=https://resonance.iqm.tech iqm_tokens_file=/etc/iqm/tokens.json partitions=quantum,debug iqm_validation_timeout=30 iqm_license_prefix=iqm_qc_ iqm_require_license=1
```

- `iqm_base_url`: Default API endpoint.
- `iqm_tokens_file`: Path to the shared token file.
- `partitions`: Comma-separated list of partitions where this plugin will run.
  If omitted, the plugin evaluates all partitions.
- `iqm_validation_timeout`: Positive whole-second timeout applied to each HTTP
  request during mandatory backend validation (default: `30`, allowed range: `1`
  to `3600`). Invalid values log a warning and use the 30-second default.
- `iqm_license_prefix`: Prefix used to derive the expected Slurm license name
  from `IQM_QC_ALIAS` (default: `iqm_qc_`). See
  [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses).
- `iqm_require_license`: When set to a truthy value
  (`1`/`true`/`yes`/`on`/`enabled`, case-insensitive), fails at launch jobs
  whose Slurm license request is missing or does not match the derived name. By
  default, mismatches log a warning and an absent request is ignored. This fails
  closed when `SLURM_JOB_LICENSES` is unavailable, so only enable it on Slurm
  23.02 or newer. See
  [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses)
  for the exact semantics. An unrecognized value logs a warning and is treated
  as off.

Ensure your main `/etc/slurm/plugstack.conf` includes your drop-in configuration
directory:

```text
include /etc/slurm/plugstack.conf.d/*.conf
```

After modifying the configuration, apply changes to the cluster:

```bash
sudo scontrol reconfigure
```

### Limiting Concurrent Access with Slurm Licenses

:::{note}
"Slurm license" here refers to Slurm's native `Licenses=`/`--licenses`
capacity-limiting scheduler resource — unrelated to the GPLv3/Apache-2.0
software licensing described elsewhere in this repository.
:::

Each QC can be modeled as a flat, cluster-wide Slurm license so that Slurm
itself enforces a concurrency limit, rather than relying on jobs to behave. This
is especially important for **on-premise QCs**: like Resonance, an on-premise
setup still runs its own internal queue, but it typically fronts single-tenant
hardware, so uncontrolled concurrency puts unnecessary pressure on that queue
and risks real hardware contention. A Slurm license lets the cluster regulate
that pressure itself, ahead of the QC's own queue.

1. Define a license pool for each QC in `/etc/slurm/slurm.conf` (a flat,
   cluster-wide pool, not tied to specific nodes — the QC is reached over the
   network from any node in the partition):

   ```text
   Licenses=iqm_qc_emerald:4
   ```

2. Users request the license alongside `--iqm-qc-alias` (see
   [Limiting Concurrent Access to a QC](#limiting-concurrent-access-to-a-qc)):

   ```bash
   srun --iqm-qc-alias=emerald --licenses=iqm_qc_emerald:1 ...
   ```

3. The plugin derives the expected license name as `<iqm_license_prefix><alias>`
   (default prefix `iqm_qc_`). Since QC aliases may themselves contain a colon
   (e.g. `emerald:mock`, as seen in the [Qiskit Integration](qiskit.md)
   examples), and Slurm's `name:count` license syntax reserves `:` as a
   separator, the plugin replaces `:` and `,` in the alias with `_` when
   deriving the name (e.g. alias `emerald:mock` → license
   `iqm_qc_emerald_mock`).
4. By default, a mismatched request logs a warning, while a missing `--licenses`
   request is a silent no-op. Setting `iqm_require_license=1` instead fails
   either case at job-step launch — after the job has already been allocated,
   not at submission time — and only takes effect if the plugin is declared
   `required` (not `optional`) in `plugstack.conf`. The hard requirement fails
   closed when `SLURM_JOB_LICENSES` is unavailable and therefore requires Slurm
   23.02 or newer.
5. Optionally, add the license name to `AccountingStorageTRES` in `slurm.conf`
   to track its usage in Slurm accounting.

### Reflecting Live QC Availability with Dynamic Licenses

:::{warning}
**This section describes an RFC-quality prototype, not a supported feature.** It
is exploratory groundwork intended to prompt a design discussion, not something
to deploy as-is on a production cluster. The tooling it describes lives at
`spank/dynamic_license_daemon.py` and `spank/license_lock.py`; these are
standalone scripts, not installed package entry points, and the SPANK plugin
itself (`iqm_spank_plugin.cpp`) is unaware of and unmodified by any of this.
:::

The static license pool above (a fixed `Licenses=iqm_qc_emerald:4` count) caps
how many jobs Slurm lets through concurrently, but that cap is not tied to
whether the QC is actually free right now. A job can pass Slurm's static count
and then still sit queued behind the QC's own internal queue. Slurm's
**Dynamic License** mechanism (Slurm 23.02+) addresses this: instead of a fixed
count, a Dynamic License's available count (`lastconsumed`) can be updated live
by an external process via

```bash
sacctmgr -i modify resource iqm_qc_emerald set lastconsumed=<0|1>
```

so the scheduler only lets a job proceed once the license shows as available.
This is the approach taken by IBM/Pasqal's QRMI (Quantum Resource Management
Interface) papers for their own Slurm integration, which flag the same static-
license gap this section addresses and propose Dynamic Licenses as the fix —
while explicitly accepting the poll-interval-vs-staleness race condition
described below as a known limitation rather than something to fully solve.

**Setup** (requires Slurm 23.02 or newer):

```bash
sacctmgr add resource name=iqm_qc_emerald count=1 cluster=<cluster> \
  allowed=100 type=license
```

#### What "availability" can actually mean here

The open question in an earlier revision of this section was whether any signal
exists for real, cross-tenant QC occupancy. A closer read of the IQM Server API
reference answers part of that, with an important caveat:
**the answer differs by deployment type**, and this prototype is designed around
that split rather than around a single assumed-universal signal.

- **No reservation/exclusive-lock endpoint exists anywhere in the API.**
  Confirmed both from this codebase (QDMI exposes no such call) and from the
  full API surface. There is no IQM-server-side primitive to "reserve" a QC for
  exclusive use.
- **Cloud/pay-as-you-go QCs do expose a real, cross-tenant queue-depth signal**:
  a "get quantum computer queue status" endpoint returning
  `{"available": [...], "queue_length": <int>}` — a genuine improvement over
  anything session-local. Its exact path is **not confirmed** (the reference
  text available while writing this section shows the schema and description but
  not the raw path), so it is deliberately not hardcoded anywhere in this PR;
  see `QueueLengthSignal` below.
- There is also a credit-priced **timeslot booking API** (list/book/cancel
  timeslots, atomic, explicitly unavailable for mock QCs) that is closer to a
  genuine reservation primitive — but it is account/credit-based and its
  applicability to **on-premise** devices is unconfirmed. Resonance is known to
  manage its own pay-as-you-go queue this way; that does not necessarily hold
  for on-prem Station Control deployments, which is exactly the deployment type
  this document's static-license section already flags as needing Slurm-side
  concurrency control the most. Treat this API as cloud/Resonance-oriented until
  someone with on-prem API access confirms otherwise.
- QDMI's own `QDMI_DEVICE_PROPERTY_STATUS` (exposed at the Python level via
  `mqt.core.fomac.Device.status()`) remains **session-local**: `IDLE` at session
  init, `BUSY` only when *that same session* submits a job. It says nothing
  about other tenants and works identically (i.e., uselessly for this purpose)
  regardless of deployment type.

**Design response: don't build the enforcement mechanism on a signal that might
not exist (on-prem) or that we can't yet confirm the shape of (cloud).**
Instead, `dynamic_license_daemon.py` treats
**the SPANK plugin's own acquire/release of a QC as the authoritative signal**,
and any QC-side status API as, at best, an optional supplementary check.
Concretely, the daemon polls one or more independent, combinable
**signal sources** (`--signal-source`, repeatable; a QC is reported unavailable
if *any* enabled source says so):

- **`lock` (default)**: reads a small file-based lease lock
  (`spank/license_lock.py`) that directly models "does a Slurm-mediated job
  currently hold this QC" — the thing we actually want to gate on, and the one
  fact that is guaranteed to be knowable uniformly across cloud and on-prem,
  because it comes from Slurm's own job lifecycle rather than from the QC. The
  lock is driven by Slurm's native `Prolog`/`Epilog` (or
  `TaskProlog`/`TaskEpilog`) script hooks — configured in `slurm.conf`, so
  **no SPANK plugin C++ changes are required**:

  ```bash
  # TaskProlog, run as the job before the task starts:
  python3 /path/to/spank/license_lock.py acquire \
    --resource-name "iqm_qc_${IQM_QC_ALIAS}" --ttl-seconds 21600

  # TaskEpilog, run after the task ends (including on failure/cancellation):
  python3 /path/to/spank/license_lock.py release \
    --resource-name "iqm_qc_${IQM_QC_ALIAS}"
  ```

  Leases carry a TTL so a crashed job (whose epilog never runs) cannot leak the
  lock forever. **This wiring is not implemented by this PR** — only the lock
  primitive and the daemon-side read of it are. Until a cluster wires up the
  Prolog/Epilog calls above, the lock is simply never held and this source
  always reports "available"; that is an explicit, logged no-op-by-default
  rather than a silent lie.
- **`qdmi-status` (legacy, off by default)**: the original session-local QDMI
  probe from an earlier revision of this prototype. Kept for backwards
  compatibility and as an optional supplementary check; not recommended as the
  sole source, for the reasons above.
- **`queue-length` (experimental, off by default, cloud/pay-as-you-go QCs only)**:
  best-effort polling of the queue-status endpoint described above. Because the
  exact path is unconfirmed, this source requires an explicit
  `--queue-status-url-template` (a URL template with a `{qc_id}` placeholder)
  rather than shipping a guessed, hardcoded path. Any request failure (wrong
  path, 404, network error, unexpected schema) degrades to "inconclusive" rather
  than "busy", so a wrong or unavailable endpoint fails open on this one source
  (the overall verdict still falls back to `lock`/other enabled sources).
  **Do not enable this on-premise** until someone with real API/dashboard access
  confirms it applies there.

**Running the daemon:**

```bash
python3 spank/dynamic_license_daemon.py --qc-alias emerald
```

Every option has an environment variable equivalent
(`IQM_DYNAMIC_LICENSE_RESOURCE_NAME`, `IQM_DYNAMIC_LICENSE_SIGNAL_SOURCES`,
`IQM_DYNAMIC_LICENSE_LOCK_DIR`, `IQM_DYNAMIC_LICENSE_QUEUE_STATUS_URL_TEMPLATE`,
`IQM_DYNAMIC_LICENSE_POLL_INTERVAL_SECONDS`, `IQM_DYNAMIC_LICENSE_CLUSTER`,
`IQM_DYNAMIC_LICENSE_DRY_RUN`, plus the existing
`IQM_BASE_URL`/`IQM_TOKEN`/`IQM_TOKENS_FILE`/`IQM_QC_ID`/`IQM_QC_ALIAS` for QC
selection and credentials); see `--help` for the full list. The `lock` source
has no extra dependencies; `qdmi-status` requires `mqt-core`, i.e.
`iqm-qdmi[qiskit]` installed in the daemon's environment.

**Open questions for reviewers:**

- **Is the `lock`-as-authoritative-signal design the right call?** It sidesteps
  the on-prem/cloud signal-availability gap above, at the cost of only tracking
  "did a Slurm job claim this QC," not the QC's true internal state (e.g. a QC
  could still be busy from non-Slurm-mediated access). Is that an acceptable
  scope for this mechanism, or is closing that gap (e.g. by confirming and
  wiring up the queue-status endpoint for cloud QCs, and finding an on-prem
  equivalent) a prerequisite before this is trusted in place of the static pool?
- **Who wires the Prolog/Epilog hooks, and how robustly?** This PR ships the
  lock primitive and documents the intended `TaskProlog`/`TaskEpilog` calls
  above, but does not wire them into any packaged Slurm configuration. Should
  that wiring ship as part of this mechanism (e.g. a documented drop-in script
  pair, or eventually a real SPANK-plugin-side acquire/release), or remain an
  administrator's responsibility?
- **Poll-interval-vs-staleness race**: even with the lock as the primary signal,
  there is an inherent window between a lock state change and the daemon's next
  poll (and `sacctmgr` update) during which Slurm's view is stale. The QRMI
  papers accept the equivalent tradeoff as a known limitation rather than
  solving it; do we accept the same here, and if so, what poll interval is an
  acceptable staleness bound for this cluster's job mix?
- **Where does the daemon run, and as whom?** A single system-level daemon per
  QC (e.g. under systemd, on a login or admin node) needs `sacctmgr` write
  access, which is a privileged operation. What identity/credentials should it
  run as, and how is that access scoped down from a general admin account?
  Separately, the lock directory needs to be writable by every node running the
  Prolog/Epilog hooks and readable by the daemon — likely a shared filesystem
  path, which has its own consistency caveats.
- **One daemon instance (and one lock) per QC**: the current design polls one QC
  per process invocation and the lock supports a single holder per resource,
  matching a `Licenses=<resource>:1` grant. A cluster with several QCs needs one
  daemon instance per QC (per alias); pool sizes greater than 1
  (`Licenses=<resource>:4`) are not supported by the lock as implemented — is
  single-holder-per-resource the right scope for this mechanism, or does it need
  to model a counted pool?
- **Interaction with `iqm_require_license`**: the SPANK plugin's fail-closed
  `iqm_require_license=1` option only checks that a matching `--licenses`
  request was made — it does not know whether that license is a static or
  Dynamic License, nor whether the daemon updating it is alive. If the daemon
  dies or falls behind, Slurm will hold jobs on a Dynamic License that never
  updates again; is a staleness/liveness check needed (e.g. a systemd watchdog,
  or Slurm-side alerting on an unchanged `lastconsumed` value) before this is
  trusted in place of the static pool?

### Troubleshooting

The plugin logs to the standard `slurmd.log` on compute nodes. Successful
activation prints a log entry when a job starts on an active partition:

```text
[iqm_spank_plugin] job=12345 partition=quantum base_url=set auth=tokens_file tokens_file_ok=yes license=iqm_qc_emerald:ok
```

**Common Issues:**

- **"Plugin metadata symbol missing"**: The plugin was compiled with
  incompatible headers or toolchain. Rebuild the plugin from source on the
  target environment.
- **Options/variables not showing up**: Verify that
  `scontrol show config | grep PlugStackConfig` references your `plugstack.conf`
  directory and that the drop-in file is read-permitted.
- **Permission Denied**: The `slurmd` process user must have read access to the
  compiled `.so` library and the specified `iqm_tokens_file`.
- **"IQM backend validation failed"**: Check that the compute node can reach
  `IQM_BASE_URL`, that its credentials are valid, and that the selected QC
  exists. Validation is mandatory and rejects the step before any task starts.

---

## Testing with Docker

To test the SPANK plugin locally without installing Slurm or configuring
services on your host machine, you can run the test suite inside an isolated
Docker container.

First, build the Docker image from the repository root:

```bash
docker build -t qdmi-spank-tests -f spank/Dockerfile .
```

Then, run the tests:

```bash
docker run --rm qdmi-spank-tests
```

To run integration tests targeting the Resonance backend, pass your `IQM_TOKEN`
as an environment variable:

```bash
docker run --rm -e IQM_TOKEN="your-token" qdmi-spank-tests
```

For a faster development loop, you can bind-mount your local workspace. This
avoids rebuilding the image when you make changes to the code or test scripts:

```bash
docker run --rm -v "$(pwd):/workspace" qdmi-spank-tests
```
