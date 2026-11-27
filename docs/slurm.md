# IQM on Slurm

A Slurm job requests a device license and uses the QDMI Client Interface to
access that device. Slurm schedules the allocation; the IQM device
implementation handles authentication and quantum execution. The same job
environment can include device implementations from several vendors.

## Submit a Qiskit job

Activate your site's workload environment and set your IQM credentials before
submission. For example, `IQM_TOKENS_FILE` can name a credential file readable
at the same path on each quantum access node. Slurm exports the submission
environment to the job. See [authentication](usage.md#authentication-methods)
for token renewal and other credential sources.

Save this workload as `bell.py`. This example requests and opens the fixed
catalogue ID `iqm.emerald.mock`:

```{code-block} python
:caption: bell.py

from qiskit import QuantumCircuit
from iqm.qdmi.qiskit import IQMBackend

backend = IQMBackend(device_id="iqm.emerald.mock")
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
result = backend.run(backend.compile(circuit), shots=100).result()
print(result.get_counts())
```

Request the Emerald Resonance mock's license on the site's quantum partition:

```bash
srun --partition=quantum --licenses=iqm.emerald.mock python bell.py
```

For requests with alternative devices, use the granted resource from Slurm's
allocation record to select the QDMI device. See
[device selection in MQSC Orchestrator](https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md#from-allocation-to-device).
IQM and other device credentials can coexist in one job environment. Keep tokens
out of command arguments, Slurm configuration, and committed scripts.

The
[offloader](python_package.md#programmatic-offloading-with-the-offloader-module)
also submits IQM sampler and estimator jobs through `srun`. IQM supports IQM
JSON and QIR. The
[MQT Core PennyLane device](https://mqt.readthedocs.io/projects/core/en/latest/qdmi/pennylane_device.html)
also compiles to IQM JSON and reconstructs the requested measurement results.

## Run an IQM iteration on Slurm

Use a disposable [MQSC Orchestrator test cluster][orchestrator-deployment] with
access to the iteration-enabled Emerald mock. In its `build/slurm` runtime
directory, copy the `iqm.emerald` entry in `resources.yaml` and name the copy
`iqm.emerald.mock`. Add `license/iqm.emerald.mock` to `AccountingStorageTRES` in
`slurm.conf` and ensure the mock is enabled in `qdmi.json`. Start the cluster
and check `scontrol show licenses iqm.emerald.mock`. Wait for the `quantum`
layer to report both `DisableHRES=false` and `DisableLayer=false`. The
controller probe uses the site's credentials in `qdmi.env`. Give the job its own
credentials as described in
[Before you start](iteration-mode.md#before-you-start).

Build from a checkout on shared storage, such as a private directory under
`/jobs` in the reference deployment. Use a Linux build environment compatible
with the quantum access node. Orchestrator's runtime image requires a separate
build environment with a C++ toolchain, CMake, and Ninja.

```bash
cmake --preset release
cmake --build --preset release --target iteration_smoke
```

From the same checkout on the login node, submit one task. The fixed Slurm
resource `iqm.emerald.mock` corresponds to the IQM alias `emerald:mock`.
Clearing `IQM_QC_ID` prevents a configured UUID from overriding that alias.

```bash
smoke="$PWD/build/release/test/iteration/iteration_smoke"
state_dir="$(mktemp -d /jobs/iqm-iteration.XXXXXXXX)"
printf 'Recovery directory: %s\n' "$state_dir"
export IQM_ITERATION_SCRATCH_FILE="${state_dir:?}/ids"
export IQM_SERVER_URL=https://resonance.iqm.tech

srun --partition=quantum --licenses=iqm.emerald.mock:1 \
  --nodes=1 --ntasks=1 --time=00:10:00 --job-name=iqm-iteration-smoke \
  --export=ALL env -u IQM_QC_ID IQM_QUANTUM_COMPUTER=emerald:mock \
  IQM_LOG_LEVEL=ERROR "$smoke" --circuits
```

After Slurm grants the resource, the workload submits a 60-second reservation
job and saves its UUID on shared storage. It attaches that ID to the first
circuit immediately and uses the result to choose the second circuit. Both
circuits have their own job handles. The workload requests graceful end,
confirms termination, and checks that IQM refuses another attached circuit.

Check the allocation state, exit code, and granted license for the past week:

```bash
sacct -X --starttime=now-7days --name=iqm-iteration-smoke,iqm-iteration-cleanup \
  --format=JobID,JobName%24,State,ExitCode,AllocTRES%60
```

Use an earlier start time for older allocations. The recovery job is named
`iqm-iteration-cleanup`. The smoke program's output reports whether it retrieved
both circuit results and whether IQM rejected the post-end submission. A
successful run leaves an empty ID file.

If cancellation or node loss interrupts cleanup, keep the ID file. After the
original Slurm job ends, request a new allocation from the login node. In a
fresh shell, return to the shared checkout, restore the same IQM credentials,
and set `state_dir` to the recovery directory printed above. The recovery task
uses the same server and Emerald mock as the original task.

```bash
smoke="$PWD/build/release/test/iteration/iteration_smoke"
export IQM_ITERATION_SCRATCH_FILE="${state_dir:?Set state_dir to the original recovery directory}/ids"
export IQM_SERVER_URL=https://resonance.iqm.tech
if [[ -s "$IQM_ITERATION_SCRATCH_FILE" ]]; then
  srun --partition=quantum --licenses=iqm.emerald.mock:1 \
    --nodes=1 --ntasks=1 --time=00:10:00 --job-name=iqm-iteration-cleanup \
    --export=ALL env -u IQM_QC_ID IQM_QUANTUM_COMPUTER=emerald:mock \
    IQM_LOG_LEVEL=ERROR "$smoke" --cleanup-only
fi
```

Cleanup removes each saved ID after confirming that its reservation has ended.
If cleanup exits nonzero, keep the file and retry. If creation was interrupted
or reported as uncertain and no ID was saved, ask the service administrator to
reconcile the reservation before creating another. See
[Handle failures](iteration-mode.md#handle-failures).

This proof of concept uses one fixed mock resource. Site deployment requires
matching the resource to the IQM target, providing shared storage, and granting
the quantum access node outbound access to IQM.

[orchestrator-deployment]: https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md

## Prepare the cluster

Install the [Python package](python_package.md) with its Qiskit adapter in the
workload environment on login and quantum access nodes:

```bash
uv pip install 'iqm-qdmi[qiskit]'
```

The driver discovers the package's catalogue, including `iqm.emerald`,
`iqm.garnet`, and their `.mock` variants. Native installations need a readable
catalogue and library on every quantum access node; retain the Python package
for the adapter. Quantum access nodes need outbound access to the IQM service.

[MQSC Orchestrator's deployment guide](https://github.com/munich-quantum-software/orchestrator/blob/main/docs/deployment.md#existing-clusters)
covers scheduler setup and device registration. Slurm controls local
allocations; IQM still controls remote access and scheduling.

Administrators can put shared non-secret settings in `/etc/mqt-core/qdmi.json`
and expose the workload environment through site defaults or a software module.
See [device configuration](usage.md#session-configuration) for quantum computer
IDs, aliases, and their precedence.
