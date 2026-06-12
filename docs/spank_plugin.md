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

## Examples

### Python & Qiskit

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

### C/C++

If you are developing low-level applications, you can use the QDMI C/C++ library. The plugin ensures that `IQM_QDMI_device_session_init` finds everything it needs.

**`device_info.cpp`**

```cpp
#include <cstdlib>
#include <iostream>
#include <vector>
#include <iqm_qdmi/device.h>
#include <iqm_qdmi/constants.h>

int main() {
    IQM_QDMI_Device_Session session = nullptr;

    // Allocate and initialize the session.
    // It will automatically read IQM_BASE_URL, IQM_TOKENS_FILE, etc. from the environment.
    if (IQM_QDMI_device_session_alloc(&session) != QDMI_SUCCESS) {
        std::cerr << "Failed to allocate session\n";
        return EXIT_FAILURE;
    }

    if (IQM_QDMI_device_session_init(session) != QDMI_SUCCESS) {
        std::cerr << "Failed to initialize session. Check your environment settings.\n";
        IQM_QDMI_device_session_free(session);
        return EXIT_FAILURE;
    }

    size_t name_size = 0;
    IQM_QDMI_device_session_query_device_property(session, QDMI_DEVICE_PROPERTY_NAME, 0, nullptr, &name_size);

    std::vector<char> name(name_size);
    IQM_QDMI_device_session_query_device_property(session, QDMI_DEVICE_PROPERTY_NAME, name_size, name.data(), nullptr);

    std::cout << "Successfully connected to: " << name.data() << '\n';

    IQM_QDMI_device_session_free(session);
    return EXIT_SUCCESS;
}
```

To compile and link against `libiqm_qdmi_device`:

```bash
g++ -std=c++20 device_info.cpp -o device_info \
  -I"$(iqm-qdmi --include_dir)" \
  -L"$(dirname "$(iqm-qdmi --lib_path)")" \
  -Wl,-rpath,"$(dirname "$(iqm-qdmi --lib_path)")" \
  -liqm_qdmi_device

srun --partition=quantum ./device_info
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
cmake -S . -B build-spank -DBUILD_IQM_QDMI_SPANK=ON
cmake --build build-spank -j
sudo cmake --install build-spank
```

This installs the compiled `.so` file to the Slurm plugin directory and places the template configuration in `plugstack.conf.d/`.

Deploy the plugin on login/submit nodes (for `srun`/`sbatch` command line parsing) and on compute nodes running `slurmd`/`slurmstepd`. Controller-only nodes do not require the plugin.

### Configuration

Configure the plugin in `plugstack.conf`. Global defaults defined here can be overridden by users at submission time.

**`/etc/slurm/plugstack.conf.d/iqm-qdmi.conf`**

```text
required /usr/lib/slurm/iqm-qdmi-spank-plugin.so \
    iqm_base_url=https://resonance.iqm.tech \
    iqm_tokens_file=/etc/iqm/tokens.json \
    partitions=quantum,debug
```

- `iqm_base_url`: Default API endpoint.
- `iqm_tokens_file`: Path to the shared token file.
- `partitions`: Comma-separated list of partitions where this plugin will run. If omitted, the plugin evaluates all partitions.

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
