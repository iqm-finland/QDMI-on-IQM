# Using IQM Quantum Computers with Slurm

If you're working on a high-performance computing (HPC) cluster managed by Slurm, our SPANK plugin makes it easy to run your quantum jobs. Whether you are a researcher running circuits or an admin managing the cluster, this guide will help you get everything set up.

The plugin acts as a bridge: it takes care of passing your IQM credentials and settings from Slurm directly into your jobs. This means you don't have to manually export environment variables every time you run a task.

---

## For Users

As a user, the plugin provides a seamless way to target IQM quantum hardware using standard Slurm commands like `srun` and `sbatch`.

### How it Works

The plugin automatically injects the necessary environment variables into your job. You can provide these values directly on the command line using new `--iqm-*` options.

### Basic Usage

If your cluster admin has set up defaults, you might not need any extra flags. However, you can always override them:

```bash
# Run a job on the 'quantum' partition, specifying a specific quantum computer
srun --partition=quantum --iqm-qc-alias=emerald python my_experiment.py
```

### Supported Options

You can use these flags with `srun` or `sbatch`:

| Option              | Environment Variable | What it's for                                                       |
| :------------------ | :------------------- | :------------------------------------------------------------------ |
| `--iqm-base-url`    | `IQM_BASE_URL`       | The URL of the IQM service you're connecting to.                    |
| `--iqm-token`       | `IQM_TOKEN`          | Your personal API token (use this for quick tests).                 |
| `--iqm-tokens-file` | `IQM_TOKENS_FILE`    | Path to a file containing your tokens (best for long-running jobs). |
| `--iqm-qc-id`       | `IQM_QC_ID`          | The unique ID of the quantum computer.                              |
| `--iqm-qc-alias`    | `IQM_QC_ALIAS`       | A friendly name (alias) for the quantum computer.                   |

### Practical Examples

#### Using Python and Qiskit

Most users will interact with our hardware via Python. The `IQMBackend` automatically picks up the settings injected by the plugin. Here is a complete "Hello World" example that creates a Bell state:

**`bell_state.py`**

```python
from iqm.qdmi.qiskit import IQMBackend
from qiskit import QuantumCircuit, transpile

# 1. Initialize the backend.
# The plugin automatically handles the URL and authentication.
backend = IQMBackend()
print(f"Connected to backend: {backend.name}")

# 2. Create a simple Bell state circuit
qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)
qc.measure_all()

# 3. Transpile and run the circuit
transpiled_qc = transpile(qc, backend)
job = backend.run(transpiled_qc, shots=100)
print(f"Job submitted! ID: {job.job_id()}")

# 4. Get and print results
result = job.result()
counts = result.get_counts()
print(f"Counts: {counts}")
```

**To run it:**

```bash
srun --partition=quantum --iqm-qc-alias=emerald python bell_state.py
```

#### Using Native C++ Applications

If you are developing low-level applications, you can use the QDMI C++ library. The plugin ensures that `IQM_QDMI_device_session_init` finds everything it needs.

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
    // It will automatically read IQM_BASE_URL, IQM_TOKEN, etc. from the environment.
    if (IQM_QDMI_device_session_alloc(&session) != QDMI_SUCCESS) {
        std::cerr << "Failed to allocate session\n";
        return EXIT_FAILURE;
    }

    if (IQM_QDMI_device_session_init(session) != QDMI_SUCCESS) {
        std::cerr << "Failed to initialize session. Check your Slurm/IQM settings.\n";
        IQM_QDMI_device_session_free(session);
        return EXIT_FAILURE;
    }

    // Query the device name
    size_t name_size = 0;
    IQM_QDMI_device_session_query_device_property(session, QDMI_DEVICE_PROPERTY_NAME, 0, nullptr, &name_size);

    std::vector<char> name(name_size);
    IQM_QDMI_device_session_query_device_property(session, QDMI_DEVICE_PROPERTY_NAME, name_size, name.data(), nullptr);

    std::cout << "Successfully connected to: " << name.data() << '\n';

    IQM_QDMI_device_session_free(session);
    return EXIT_SUCCESS;
}
```

**To compile and run:**

You'll need to link against the `iqm_qdmi_device` library. If you installed the `iqm-qdmi` Python package, you can use it to find the correct paths:

```bash
# Compile using paths from the iqm-qdmi utility
g++ -std=c++20 device_info.cpp -o device_info \
  -I"$(iqm-qdmi --include_dir)" \
  -L"$(dirname "$(iqm-qdmi --lib_path)")" \
  -Wl,-rpath,"$(dirname "$(iqm-qdmi --lib_path)")" \
  -liqm_qdmi_device

# Run it on the cluster
srun --partition=quantum ./device_info
```

#### Direct Circuit Submission (No-Code)

For users who already have a quantum circuit in IQM JSON format, the plugin can
submit the file directly as if it were an executable. The SPANK plugin
intercepts the execution and wraps it using the `iqm-qdmi-runner` utility.

Example with an IQM JSON file:

```console
srun --partition=quantum ./my_circuit.json
```

Batch submission:

```bash
#!/bin/bash
#SBATCH --partition=quantum

./my_circuit.json
```

In this mode, the results (histogram counts) are printed directly to `stdout` as a JSON object:

```json
{ "00": 512, "11": 488 }
```

This workflow avoids the need to write any Python or C++ code for simple circuit execution tasks.

When running from a source checkout, repository examples can be submitted directly, for
example:

```console
srun --partition=quantum python examples/ghz.py --backend iqm
```

---

## For HPC Administrators

The SPANK plugin is designed to be "shallow" and lightweight. It doesn't handle complex logic; it simply parses arguments and ensures the right environment is ready for the user's job.

### Installation

Building the plugin is straightforward using CMake:

```bash
# Build the plugin
cmake -S . -B build-spank -DBUILD_IQM_QDMI_SPANK=ON
cmake --build build-spank -j

# Install to the system
sudo cmake --install build-spank
```

By default, it installs the `.so` file to your Slurm plugin directory and a template configuration to `plugstack.conf.d/`.

### Configuration

The plugin is configured via `plugstack.conf`. You can set global defaults here so that users don't have to provide them every time.

**Example: `/etc/slurm/plugstack.conf.d/iqm-qdmi.conf`**

```text
required /usr/lib/slurm/iqm-qdmi-spank-plugin.so \
    iqm_base_url=https://resonance.iqm.tech \
    iqm_tokens_file=/etc/iqm/tokens.json \
    partitions=quantum,debug
```

- `iqm_base_url`: The default endpoint for your site.
- `iqm_tokens_file`: A system-wide token file (optional).
- `partitions`: A comma-separated list of partitions where this plugin should be active.

### Activation

1.  Make sure your main `plugstack.conf` includes your config directory:
    ```text
    include /etc/slurm/plugstack.conf.d/*.conf
    ```
2.  Apply the changes:
    ```bash
    sudo scontrol reconfigure
    ```

### Monitoring and Troubleshooting

The plugin logs its activity to the standard Slurm logs (`slurmd.log`). When a job starts on an active partition, you'll see a structured summary line:

`[iqm_spank_plugin] job=12345 partition=quantum base_url=set auth=tokens_file tokens_file_ok=yes`

**Common issues:**

- **"Plugin metadata symbol missing":** Ensure the plugin was compiled with the correct headers and linked properly.
- **Variables not showing up:** Check if `scontrol show config | grep PlugStackConfig` points to the file you edited.
- **Permission denied:** Ensure the `slurmd` user can read the plugin `.so` file and any configured `iqm_tokens_file`.

For a quick check if it's working, ask a user to run:

```bash
srun --partition=quantum env | grep IQM_
```
