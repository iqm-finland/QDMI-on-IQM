# Workflow Guide

This guide collects end-to-end Qiskit workflows built on top of the packaged {py:class}`~iqm.qdmi.qiskit.IQMBackend`.
Unlike the C API examples in the general [usage guide](usage.md), these workflows exercise the `qiskit` extra together with MQT Core's sampler and estimator primitives.

The corresponding implementations live in the use-case test tree:

- `test/use_cases/mqt_bench/test_mqt_bench.py`
- `test/use_cases/qsci/test_qsci.py`

These workflows are intentionally kept out of the default Python test path.
They are heavier than the wrapper tests in `test/python/` and are meant to be run explicitly when you want a realistic showcase of the QDMI-backed Qiskit integration.

## Running the Workflows

Both workflows use the same environment-variable contract as the rest of the package:

- `IQM_BASE_URL`: IQM server endpoint.
- `IQM_TOKEN` or `RESONANCE_API_KEY`: authentication token.
- `IQM_TOKENS_FILE`: optional authentication file.
- `IQM_QC_ALIAS` or `IQM_QC_ID`: optional explicit target selection.

Install the test dependencies and run the workflow suite explicitly with `uv`:

```bash
uv run --group test pytest test/use_cases
```

You can also focus on one showcase family at a time:

```bash
uv run --group test pytest test/use_cases -m mqt_bench
uv run --group test pytest test/use_cases -m qsci
```

## MQT Bench Showcase

[MQT Bench](https://mqt.readthedocs.io/projects/bench/) provides a large catalog of benchmark circuits.
In this repository, it serves as a use-case sampler showcase: the test suite generates benchmark circuits, transpiles them for the selected IQM target, executes them through {py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler`, and validates the observed bitstring distribution.

The current workflow covers:

- GHZ states
- Deutsch-Jozsa
- Quantum Fourier Transform
- Graph states
- W-states

### Implementation Pattern

The use-case tests in `test/use_cases/mqt_bench/test_mqt_bench.py` follow this structure:

```{code-cell} ipython3
from mqt.bench import BenchmarkLevel, get_benchmark
from iqm.qdmi.qiskit import IQMBackend

backend = IQMBackend()

circuit = get_benchmark(
    benchmark="ghz",
    level=BenchmarkLevel.ALG,
    circuit_size=3,
)
if not any(instruction.operation.name == "measure" for instruction in circuit.data):
    circuit.measure_all()

transpiled = transpile(circuit, backend=backend, optimization_level=3)
job = backend.sampler().run([(transpiled,)], shots=1024)
counts = job.result()[0].data["meas"].get_counts()
```

### Runtime Knob

The benchmark suite exposes one primary tuning knob:

- `IQM_MQT_BENCH_SHOTS`: number of shots per benchmark circuit with a default of `1024`.

For quick smoke runs, reduce it.
For stronger distribution checks on noisier targets, increase it.

## QSCI Showcase

The **Quantum-Selected Configuration Interaction (QSCI)** workflow combines a variational quantum step with classical postprocessing to estimate a molecular ground-state energy.

The QSCI use-case flow is:

1. Build the electronic-structure problem with Qiskit Nature.
2. Construct a UCCSD ansatz and map the Hamiltonian with Jordan-Wigner.
3. Optimize the ansatz with {py:meth}`~iqm.qdmi.qiskit.IQMBackend.estimator` through VQE.
4. Sample the trained ansatz with {py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler`.
5. Postprocess the measured configurations classically in `test/use_cases/qsci/postprocess.py`.

### Implementation Pattern

The use-case implementation in `test/use_cases/qsci/test_qsci.py` uses {py:class}`~iqm.qdmi.qiskit.IQMBackend` together with Qiskit Nature and Qiskit Algorithms to run the full workflow end to end:

```{code-cell} ipython3
from qiskit_algorithms import VQE
from qiskit_algorithms.optimizers import L_BFGS_B
from qiskit_nature.second_q.circuit.library import HartreeFock, UCCSD
from qiskit_nature.second_q.drivers import PySCFDriver
from qiskit_nature.second_q.mappers import JordanWignerMapper

from iqm.qdmi.qiskit import IQMBackend

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

estimator = backend.estimator(options={"default_shots": 2048})
vqe = VQE(estimator, ansatz, L_BFGS_B(maxiter=30))
result = vqe.compute_minimum_eigenvalue(operator=observable)

trained_ansatz = ansatz.assign_parameters(result.optimal_parameters)
sample_circuit = trained_ansatz.copy()
sample_circuit.measure_all()
transpiled = transpile(sample_circuit, backend=backend, optimization_level=3)
counts = backend.sampler().run([(transpiled,)], shots=2048).result()[0].data["meas"].get_counts()
```

The classical reduction step is intentionally kept in a separate module so the use-case test remains easy to read while still showing the full workflow.

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

The use-case workflows in `test/use_cases/` build on top of that layer and are intended to answer a different question: what do realistic application-level workflows look like when they utilize the backend?
