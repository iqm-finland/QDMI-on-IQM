# End-User Showcases

This page showcases end-to-end Qiskit demonstrations built on top of the packaged {py:class}`~iqm.qdmi.qiskit.IQMBackend` and the generic QDMI backend surface exposed by MQT Core.
Unlike the C API examples in the general [usage guide](usage.md), these demonstrations focus on how end users can apply the `qiskit` extra together with MQT Core's generic sampler and estimator primitives.

The corresponding implementations live in the showcase test tree:

- `test/showcases/mqt_bench/test_mqt_bench.py`
- `test/showcases/qsci/test_qsci.py`

These showcases are intentionally kept out of the default Python test path.
They are heavier than the wrapper tests in `test/python/` and are meant to be run explicitly when you want a realistic end-user demonstration of the QDMI-backed Qiskit integration.

## Running the Showcases

The showcase suite defaults to the IQM backend path and uses the same environment-variable contract as the rest of the package:

- `IQM_BASE_URL`: IQM server endpoint.
- `IQM_TOKEN` or `RESONANCE_API_KEY`: authentication token.
- `IQM_TOKENS_FILE`: optional authentication file.
- `IQM_QC_ALIAS` or `IQM_QC_ID`: optional explicit target selection.
- `IQM_SHOWCASE_BACKEND`: showcase backend selection. Supported values are `iqm` (default) and `ddsim`.

Install the showcase dependencies and run the showcase suite explicitly with `uv`.

For the default IQM-backed path:

```bash
export IQM_BASE_URL="https://desired-iqm-server.com"
export RESONANCE_API_KEY="your-api-key"
uv run --group showcase pytest test/showcases
```

For the DDSIM-backed path, opt in explicitly and omit the IQM credentials:

```bash
IQM_SHOWCASE_BACKEND=ddsim uv run --group showcase pytest test/showcases
```

You can also focus on one showcase family at a time:

```bash
uv run --group showcase pytest test/showcases -m mqt_bench
uv run --group showcase pytest test/showcases -m qsci
```

:::note
The QSCI showcase depends on PySCF, which is [not supported on Windows](https://pyscf.org/user/install.html).
:::

:::{important}
The IQM-backed showcase assertions are tuned for real IQM QPUs.
If you point `IQM_QC_ALIAS` or `IQM_QC_ID` at a mock IQM target, the showcase is still executed, but the stricter showcase assertions may fail.
Use `IQM_SHOWCASE_BACKEND=ddsim` if you want a validation path without IQM credentials.
:::

## MQT Bench Showcase

[MQT Bench](https://mqt.readthedocs.io/projects/bench/) provides a large catalog of benchmark circuits.
In this repository, it serves as a sampler showcase: the test suite generates benchmark circuits, transpiles them for the selected showcase backend, executes them through MQT Core's generic QDMI sampler primitive, and validates the observed bitstring distribution.

The current showcase covers:

- GHZ states
- Deutsch-Jozsa
- Quantum Fourier Transform
- Graph states
- W-states

### Implementation Pattern

The showcase tests in `test/showcases/mqt_bench/test_mqt_bench.py` follow this structure:

```{code-cell} ipython3
import os

from mqt.bench import BenchmarkLevel, get_benchmark
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler
from qiskit import transpile

from iqm.qdmi.qiskit import IQMBackend

backend_kind = os.getenv("IQM_SHOWCASE_BACKEND", "iqm").strip().lower()
if backend_kind == "ddsim":
    backend = QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
else:
    backend = IQMBackend()

circuit = get_benchmark(
    benchmark="ghz",
    level=BenchmarkLevel.ALG,
    circuit_size=3,
)
if not any(instruction.operation.name == "measure" for instruction in circuit.data):
    circuit.measure_all()

if backend_kind == "ddsim":
    transpiled = transpile(
        circuit, basis_gates=sorted(backend.operation_names), optimization_level=1
    )
else:
    transpiled = transpile(circuit, backend=backend, optimization_level=3)

sampler = QDMISampler(backend, default_shots=1024)
job = sampler.run([(transpiled,)], shots=1024)
counts = job.result()[0].data
```

### Runtime Knob

The benchmark suite exposes one primary tuning knob:

- `IQM_MQT_BENCH_SHOTS`: number of shots per benchmark circuit with a default of `1024`.

For quick smoke runs, reduce it.
For stronger distribution checks on noisier targets, increase it.

## QSCI Showcase

The **Quantum-Selected Configuration Interaction (QSCI)** showcase combines a variational quantum step with classical postprocessing to estimate a molecular ground-state energy.

The QSCI showcase flow is:

1. Build the electronic-structure problem with Qiskit Nature.
2. Construct a UCCSD ansatz and map the Hamiltonian with Jordan-Wigner.
3. Optimize the ansatz with Qiskit's `BackendEstimator` wrapper over the selected showcase backend so VQE can use the estimator V1 interface it still expects.
4. Sample the trained ansatz with MQT Core's generic QDMI sampler primitive.
5. Postprocess the measured configurations classically in `test/showcases/qsci/postprocess.py`.

### Implementation Pattern

The showcase implementation in `test/showcases/qsci/test_qsci.py` selects the showcase backend first and then uses the same Qiskit Nature and Qiskit Algorithms flow on top of it:

```{code-cell} ipython3
import os

from qiskit_algorithms import VQE
from qiskit_algorithms.optimizers import L_BFGS_B
from qiskit.primitives import BackendEstimator
from qiskit_nature.second_q.circuit.library import HartreeFock, UCCSD
from qiskit_nature.second_q.drivers import PySCFDriver
from qiskit_nature.second_q.mappers import JordanWignerMapper
from mqt.core.plugins.qiskit.provider import QDMIProvider
from qiskit import transpile

from iqm.qdmi.qiskit import IQMBackend

backend_kind = os.getenv("IQM_SHOWCASE_BACKEND", "iqm").strip().lower()
if backend_kind == "ddsim":
    backend = QDMIProvider().get_backend("MQT Core DDSIM QDMI Device")
else:
    backend = IQMBackend()

driver = PySCFDriver(atom="H 0 0 0; H 0 0 1;", basis="sto-3g")
problem = driver.run()

mapper = JordanWignerMapper()
initial_state = HartreeFock(problem.num_spatial_orbitals, problem.num_particles, mapper)
ansatz = UCCSD(
    problem.num_spatial_orbitals,
    problem.num_particles,
    mapper,
    initial_state=initial_state,
)
observable = mapper.map(problem.second_q_ops()[0])

# qiskit_algorithms.VQE still expects the Estimator V1 interface.
# DDSIM runs pre-transpile the ansatz and skip estimator-side transpilation,
# while IQM runs let BackendEstimator transpile against the hardware target.
if backend_kind == "ddsim":
    ansatz = transpile(
        ansatz, basis_gates=sorted(backend.operation_names), optimization_level=1
    )
    estimator = BackendEstimator(
        backend=backend, options={"shots": 2048}, skip_transpilation=True
    )
else:
    estimator = BackendEstimator(backend=backend, options={"shots": 2048})

vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=30))
result = vqe.compute_minimum_eigenvalue(operator=observable)

trained_ansatz = ansatz.assign_parameters(result.optimal_parameters)
sample_circuit = trained_ansatz.copy()
sample_circuit.measure_all()
if backend_kind == "ddsim":
    transpiled = transpile(
        sample_circuit,
        basis_gates=sorted(backend.operation_names),
        optimization_level=1,
    )
else:
    transpiled = transpile(sample_circuit, backend=backend, optimization_level=3)
```

The classical reduction step is intentionally kept in a separate module so the showcase test remains easy to read while still showing the full flow.

### Runtime Knobs

The QSCI suite exposes a few environment variables, so you can trade runtime for precision:

- `IQM_QSCI_MAXITER`: VQE optimizer iterations.
- `IQM_QSCI_SHOTS`: sampler and estimator shot budget.
- `IQM_QSCI_CUTOFF`: number of configurations retained for classical postprocessing.
- `IQM_QSCI_ENERGY_TOLERANCE`: accepted deviation from the exact reference energy.

The defaults are tuned for showcase runs, not for aggressive convergence.

## Relationship to the Thin Python Tests

The lightweight tests in `test/python/test_qiskit_backend.py` remain the fast test layer for the backend itself.
They verify creation plus direct `backend.run`, `backend.sampler`, and `backend.estimator` calls.

The showcases in `test/showcases/` build on top of that layer and are intended to answer a different question: what do realistic application-level showcases look like when they utilize the backend?
