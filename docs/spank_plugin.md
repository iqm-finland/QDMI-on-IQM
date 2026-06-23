# Slurm SPANK Plugin

The Slurm SPANK plugin for QDMI-on-IQM integrates quantum computer access into the Slurm workload manager, allowing administrators to expose QPUs as scheduled Slurm Generic Resources (GRES) and automatically inject necessary connection settings (`IQM_*` environment variables) into job steps.

---

## For Users

The plugin registers `--iqm-*` command-line options for standard Slurm submission tools (`srun`, `sbatch`, `salloc`). When these options are provided, the plugin translates them into the corresponding environment variables for the job tasks.

### Supported Options

| Option               | Environment Variable | Description                                           |
| :------------------- | :------------------- | :---------------------------------------------------- |
| `--iqm-base-url`     | `IQM_BASE_URL`       | The endpoint URL of the IQM service.                  |
| `--iqm-tokens-file`  | `IQM_TOKENS_FILE`    | Path to the file containing your access tokens.       |
| `--iqm-qc-id`        | `IQM_QC_ID`          | The unique identifier of the target quantum computer. |
| `--iqm-qc-alias`     | `IQM_QC_ALIAS`       | The alias of the target quantum computer.             |
| `--iqm-gres-name`    | (None)               | Custom GRES resource name for QPU mapping.            |
| `--iqm-list-devices` | (None)               | Query and print available IQM quantum computers.      |

### QPU Resource Discovery

Users can discover which QPUs are configured and available on the cluster in two ways:

1. **Via standard Slurm commands** (if the administrator has set up GRES for QPUs):

   ```bash
   sinfo -o "%N %G"
   ```

   This displays nodes and their configured QPU resources (e.g. `gres/qpu:emerald:1`).

2. **Via the SPANK plugin's custom option**:
   ```bash
   srun --iqm-list-devices /bin/true
   ```
   This queries the configured IQM endpoint, lists the available devices and their status, and exits cleanly.

### Slurm GRES Automatic Mapping

If the cluster administrator has configured the QPU as a Generic Resource (GRES), users can submit jobs requesting specific QPU resources via the standard Slurm `--gres` option. The SPANK plugin will automatically detect the allocated QPU and map it to `IQM_QC_ALIAS`:

```bash
# The plugin detects 'emerald' from GRES and sets IQM_QC_ALIAS=emerald automatically
srun --gres=qpu:emerald:1 python bell_state.py
```

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

---

## For HPC Administrators

The SPANK plugin is a lightweight C++ module that intercepts job launches to parse options and inject environment variables. It does not implement scheduler policy or handle backend-side queue management.

### Compatibility and Requirements

- **Slurm Version**: Slurm 17.11 or newer.
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
    iqm_gres_name=qpu
```

- `iqm_base_url`: Default API endpoint.
- `iqm_tokens_file`: Path to the shared token file.
- `partitions`: Comma-separated list of partitions where this plugin will run. If omitted, the plugin evaluates all partitions.
- `iqm_gres_name`: The Generic Resource (GRES) name used to track QPUs in Slurm (defaults to `qpu`).

### Scheduling QPUs as Slurm Generic Resources (GRES)

To schedule QPUs natively under Slurm, administrators should configure them as Generic Resources (GRES).

1. Register the custom GRES type in `/etc/slurm/slurm.conf`:
   ```text
   GresTypes=qpu
   ```
2. Configure available QPUs in `/etc/slurm/gres.conf` on compute nodes:
   ```text
   NodeName=node[1-4] Name=qpu Type=emerald Count=1
   NodeName=node[5-8] Name=qpu Type=garnet Count=1
   ```

Ensure your main `/etc/slurm/plugstack.conf` includes your drop-in configuration directory:

```text
include /etc/slurm/plugstack.conf.d/*.conf
```

After modifying the configuration, apply changes to the cluster:

```bash
sudo scontrol reconfigure
```

### Troubleshooting

The plugin logs to the standard `slurmd.log` on compute nodes. Successful activation prints a log entry when a job starts on an active partition:

`[iqm_spank_plugin] job=12345 partition=quantum base_url=set auth=tokens_file tokens_file_ok=yes`

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
