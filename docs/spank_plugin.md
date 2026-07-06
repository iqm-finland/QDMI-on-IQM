# Slurm SPANK Plugin

The Slurm SPANK plugin for QDMI-on-IQM simplifies running quantum jobs on clusters by automatically propagating `IQM_*` environment variables to job steps. This avoids manual `export` statements in job scripts and enables administrators to configure global defaults and partition-gated access.

---

## For Users

The plugin registers `--iqm-*` command-line options for standard Slurm submission tools (`srun`, `sbatch`, `salloc`). When these options are provided, the plugin translates them into the corresponding environment variables for the job tasks.

### Supported Options

| Option              | Environment Variable | Description                                           |
| :------------------ | :------------------- | :---------------------------------------------------- |
| `--iqm-base-url`    | `IQM_BASE_URL`       | The endpoint URL of the IQM service.                  |
| `--iqm-tokens-file` | `IQM_TOKENS_FILE`    | Path to the file containing your access tokens.       |
| `--iqm-qc-id`       | `IQM_QC_ID`          | The unique identifier of the target quantum computer. |
| `--iqm-qc-alias`    | `IQM_QC_ALIAS`       | The alias of the target quantum computer.             |

### Credential Security

Direct token passing is intentionally unsupported on the command line. Slurm command arguments may be captured in shell history, process listings, scheduler logs, or accounting records.

To run authenticated jobs:

1. Save your tokens to a secure file.
2. Pass the path to this file using the `--iqm-tokens-file` option.
3. Ensure the token file is readable on the compute nodes where the tasks execute.

---

## Example

The {py:class}`~iqm.qdmi.qiskit.IQMBackend` class automatically resolves configuration values from the environment variables injected by the plugin.

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

If your administrator has configured a Slurm license for the target QC (see [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses) below), request it alongside `--iqm-qc-alias` using Slurm's native `--licenses`/`-L` option:

```bash
srun --partition=quantum --iqm-qc-alias=emerald --licenses=iqm_qc_emerald:1 python bell_state.py
```

This matters most for **on-premise QCs**: unlike the cloud Resonance API, which queues overlapping requests on its own, an on-premise QC is typically single-tenant hardware with no such queue, so Slurm itself must serialize access. If you omit `--licenses` on a cluster where it's expected, the plugin logs a warning (or, if the administrator has set `iqm_require_license=1`, fails your job step at launch instead — after it has already been allocated, not at submission time).

### Executing via CLI Scripts

Alternatively, if you already have serialized circuits (in QPY format), you can execute them directly on the cluster using the packaged CLI scripts (see the [Sampler and Estimator CLI Utilities](python_package.md#sampler-and-estimator-cli-utilities) documentation for details) without writing any custom Python code:

```bash
# Run a serialized circuit using the sampler CLI
srun --partition=quantum --iqm-qc-alias=emerald iqm-sampler bell.qpy --shots 100

# Estimate parameters using the estimator CLI
srun --partition=quantum --iqm-qc-alias=emerald iqm-estimator ansatz.qpy observable.pkl --maxiter 10
```

---

## For HPC Administrators

The SPANK plugin is a lightweight C++ module that intercepts job launches to parse options and inject environment variables. It does not implement scheduler policy or handle backend-side queue management.

### Compatibility and Requirements

- **Slurm Version**: Slurm 17.11 or newer. The optional Slurm license alignment check (see [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses)) additionally requires Slurm 23.02 or newer, since it relies on the `SLURM_JOB_LICENSES` job environment variable; on older Slurm versions it is a silent no-op.
- **C++ Compiler**: C++20 standard library support (GCC 13+ or Clang 16+).
- **Compilation Constraint**: SPANK plugins are tied to the Slurm daemon ABI. You must compile the plugin against the target cluster's Slurm header files (`slurm/spank.h`) and rebuild the plugin after any major/minor Slurm upgrades.

### Installation

To compile and install the plugin from the repository root:

```bash
cmake -S . -B build-spank -DBUILD_IQM_SPANK=ON
cmake --build build-spank --target iqm-spank-plugin --parallel
sudo cmake --install build-spank --component iqm-spank-plugin
```

This installs the compiled `.so` file to the Slurm plugin directory and places the template configuration in `plugstack.conf.d/`.

Deploy the plugin on login/submit nodes (for `srun`/`sbatch` command line parsing) and on compute nodes running `slurmd`/`slurmstepd`. Controller-only nodes do not require the plugin.

### Configuration

Configure the plugin in `plugstack.conf`. Global defaults defined here can be overridden by users at submission time.

**`/etc/slurm/plugstack.conf.d/iqm-qdmi.conf`**

```text
required /usr/lib/slurm/iqm-spank-plugin.so \
    iqm_base_url=https://resonance.iqm.tech \
    iqm_tokens_file=/etc/iqm/tokens.json \
    partitions=quantum,debug \
    iqm_license_prefix=iqm_qc_ \
    iqm_require_license=1
```

- `iqm_base_url`: Default API endpoint.
- `iqm_tokens_file`: Path to the shared token file.
- `partitions`: Comma-separated list of partitions where this plugin will run. If omitted, the plugin evaluates all partitions.
- `iqm_license_prefix`: Prefix used to derive the expected Slurm license name from `IQM_QC_ALIAS` (default: `iqm_qc_`). See [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses).
- `iqm_require_license`: When set to a truthy value (`1`/`true`/`yes`/`on`/`enabled`, case-insensitive), fails at launch jobs whose Slurm license request does not match the derived name, instead of only logging a warning (default: off). See [Limiting Concurrent Access with Slurm Licenses](#limiting-concurrent-access-with-slurm-licenses) for the exact semantics. An unrecognized value logs a warning and is treated as off.

Ensure your main `/etc/slurm/plugstack.conf` includes your drop-in configuration directory:

```text
include /etc/slurm/plugstack.conf.d/*.conf
```

After modifying the configuration, apply changes to the cluster:

```bash
sudo scontrol reconfigure
```

### Limiting Concurrent Access with Slurm Licenses

:::{note}
"Slurm license" here refers to Slurm's native `Licenses=`/`--licenses` capacity-limiting scheduler resource — unrelated to the GPLv3/Apache-2.0 software licensing described elsewhere in this repository.
:::

Each QC can be modeled as a flat, cluster-wide Slurm license so that Slurm itself enforces a concurrency limit, rather than relying on jobs to behave. This is especially important for **on-premise QCs**: unlike the cloud Resonance API, which absorbs overlapping requests in its own queue, an on-premise QC is typically single-tenant hardware, so uncontrolled concurrency risks real hardware contention.

1. Define a license pool for each QC in `/etc/slurm/slurm.conf` (a flat, cluster-wide pool, not tied to specific nodes — the QC is reached over the network from any node in the partition):

   ```text
   Licenses=iqm_qc_emerald:4
   ```

2. Users request the license alongside `--iqm-qc-alias` (see [Limiting Concurrent Access to a QC](#limiting-concurrent-access-to-a-qc)):

   ```bash
   srun --iqm-qc-alias=emerald --licenses=iqm_qc_emerald:1 ...
   ```

3. The plugin derives the expected license name as `<iqm_license_prefix><alias>` (default prefix `iqm_qc_`). Since QC aliases may themselves contain a colon (e.g. `emerald:mock`, as seen in the [Qiskit Integration](qiskit.md) examples), and Slurm's `name:count` license syntax reserves `:` as a separator, the plugin replaces `:` and `,` in the alias with `_` when deriving the name (e.g. alias `emerald:mock` → license `iqm_qc_emerald_mock`).
4. By default, a mismatch (or a missing `--licenses` request) only logs a warning. Setting `iqm_require_license=1` instead fails the job step at launch — after the job has already been allocated, not at submission time — and only takes effect if the plugin is declared `required` (not `optional`) in `plugstack.conf`.
5. Optionally, add the license name to `AccountingStorageTRES` in `slurm.conf` to track its usage in Slurm accounting.

### Troubleshooting

The plugin logs to the standard `slurmd.log` on compute nodes. Successful activation prints a log entry when a job starts on an active partition:

`[iqm_spank_plugin] job=12345 partition=quantum base_url=set auth=tokens_file tokens_file_ok=yes license=iqm_qc_emerald:ok`

**Common Issues:**

- **"Plugin metadata symbol missing"**: The plugin was compiled with incompatible headers or toolchain. Rebuild the plugin from source on the target environment.
- **Options/variables not showing up**: Verify that `scontrol show config | grep PlugStackConfig` references your `plugstack.conf` directory and that the drop-in file is read-permitted.
- **Permission Denied**: The `slurmd` process user must have read access to the compiled `.so` library and the specified `iqm_tokens_file`.

---

## Testing with Docker

To test the SPANK plugin locally without installing Slurm or configuring services on your host machine, you can run the test suite inside an isolated Docker container.

First, build the Docker image from the repository root:

```bash
docker build -t qdmi-spank-tests -f spank/Dockerfile .
```

Then, run the tests:

```bash
docker run --rm qdmi-spank-tests
```

To run integration tests targeting the Resonance backend, pass your `IQM_TOKEN` as an environment variable:

```bash
docker run --rm -e IQM_TOKEN="your-token" qdmi-spank-tests
```

For a faster development loop, you can bind-mount your local workspace. This avoids rebuilding the image when you make changes to the code or test scripts:

```bash
docker run --rm -v "$(pwd):/workspace" qdmi-spank-tests
```
