# Running Example Quantum Workloads on IQM Hardware

Welcome to the end-user tutorial.
This guide walks you step by step through driving real quantum workloads on IQM systems using the packaged {py:class}`~iqm.qdmi.qiskit.IQMBackend`.

Whether you want to estimate molecular ground-state energies with [QSCI][qsci] or benchmark hardware with [MQT Bench][mqt-bench], the example scripts in this repository provide a practical starting point.
This tutorial focuses on two application areas:

- **Quantum chemistry:** using [QSCI][qsci] and [Qiskit Nature][qiskit-nature] to estimate the ground-state energy of an H2 molecule.
- **Benchmarking:** running [MQT Bench][mqt-bench] circuits such as [GHZ states][ghz-state], [Deutsch-Jozsa][deutsch-jozsa], [QFT][quantum-fourier-transform], [graph states][graph-state], and [W states][w-state].

:::{important}
The example scripts live in the source repository and are not shipped with the PyPI wheel.
:::

## Configure Your Environment

The IQM-backed path relies on a small environment-variable contract to authenticate and route your jobs.
Before running any of the examples, make sure the following variables are set as needed:

- `IQM_BASE_URL`: The endpoint of the IQM server you are targeting.
- `IQM_TOKEN` or `RESONANCE_API_KEY`: Your authentication token.
- `IQM_TOKENS_FILE`: Optional path to a file containing your authentication credentials.
- `IQM_QC_ALIAS` or `IQM_QC_ID`: Optional explicit selection of the target quantum computer.

## Choose Your Installation Path

Depending on how you are installing the package, the commands differ slightly.
Pick the path that matches your setup.

::::{tab-set}
:::{tab-item} Option A: Installing `iqm-qdmi` From PyPI

Install the package with the Qiskit integration enabled:

```console
$ uv pip install "iqm-qdmi[qiskit]"
```

Because the example scripts are not packaged inside the wheel, download or clone the [examples directory][examples-directory].
Once you have that directory locally, use `uv run --script` so the script's embedded dependencies are correctly resolved, e.g.:

```console
$ uv run --script examples/qsci_h2.py --shots 256 --maxiter 5 --cutoff 4
$ uv run --script examples/ghz.py --shots 128
```

:::
:::{tab-item} Option B: Cloning the Repository Locally

If you are working from a local checkout, you can either run the full suite or launch individual scripts directly through the local `iqm-qdmi` CLI:

```console
# Run the entire suite
$ uvx nox -s examples

# Run specific examples
$ uvx --from . iqm-qdmi examples/qsci_h2.py --shots 256 --maxiter 5 --cutoff 4
$ uvx --from . iqm-qdmi examples/ghz.py --shots 128
```

:::
::::

## Quantum Chemistry

Our first workload is a quantum chemistry application that estimates the ground-state energy of a hydrogen molecule.
The script `examples/qsci_h2.py` follows a [Quantum-Selected Configuration Interaction][qsci]-style workflow: a hybrid quantum-classical approach that combines variational optimization, circuit sampling, and classical diagonalization in a reduced subspace.
To build the chemistry problem, it leans on [Qiskit Nature][qiskit-nature] to map an electronic-structure model to qubit operators.

By running `examples/qsci_h2.py`, you will:

1. Build an electronic-structure problem for an H2 molecule.
2. Map the physical system to qubits with [Qiskit Nature][qiskit-nature].
3. Optimize a UCCSD ansatz against an IQM backend.
4. Sample the circuit to gather bitstrings.
5. Reconstruct the energy estimate with `examples/qsci_postprocess.py` and compare it against an exact reference.

When the run finishes, the script prints the sampled shot count, the QSCI energy, the exact reference energy, and the absolute difference between them.
QSCI is a great first end-to-end workload beyond a simple toy circuit.

:::{note}
The QSCI example depends on PySCF for classical chemistry calculations, and [PySCF is not supported on Windows](https://pyscf.org/user/install.html).
:::

### The CLI Surface

The QSCI script exposes a compact CLI:

- `--shots`: Controls how many samples are collected from the trained ansatz.
- `--maxiter`: Sets the maximum number of optimizer iterations during the VQE step.
- `--cutoff`: Limits how many sampled basis states are kept during the classical QSCI reconstruction.

Together, these options let you trade runtime against refinement: higher shot counts, more optimizer iterations, and a larger reconstruction cutoff generally make the workflow more expensive but can improve the quality of the final estimate.

## Standardized Benchmarks

To understand how the backend behaves on standard circuits, move on to [MQT Bench][mqt-bench].
MQT Bench is an open-source benchmark suite that collects representative quantum algorithms across several abstraction levels.
In this repository, the benchmark scripts show how to generate those circuits, transpile them for the selected target, execute them through {py:class}`~mqt.core.plugins.qiskit.sampler.QDMISampler`, and inspect the resulting bitstring distributions.

The current benchmark scripts cover the following algorithms:

- `examples/ghz.py`: Prepares a [GHZ state][ghz-state], a highly entangled state that serves as a strong baseline test for multi-qubit entanglement fidelity.
- `examples/deutsch_jozsa.py`: Implements the [Deutsch-Jozsa algorithm][deutsch-jozsa], one of the earliest examples of a quantum algorithm with an exponential query advantage over its classical counterpart.
- `examples/qft.py`: Computes the [Quantum Fourier Transform][quantum-fourier-transform], a central building block used in algorithms such as phase estimation and Shor's algorithm.
- `examples/graphstate.py`: Generates [graph states][graph-state], an important family of entangled states that also serve as key resources for [measurement-based quantum computing][measurement-based-quantum-computing].
- `examples/wstate.py`: Creates a [W state][w-state], a multipartite entangled state that retains pairwise entanglement even if one qubit is lost.

### The CLI Surface

All benchmark scripts expose a compact, consistent CLI:

- `--backend`: Selects `iqm` for hardware runs or `sim` for simulator runs.
- `--shots`: Controls how many samples are collected from the executed circuit.
- `--num-qubits`: Adjusts the problem size for the benchmark families that support it.

### Code Example: Generating a GHZ State

The following snippet shows the GHZ benchmark in full.
The same overall architecture carries over to the other MQT Bench scripts; only the benchmark-generation logic and the printed result summaries change.

```{literalinclude} ../examples/ghz.py
:language: python
:caption: examples/ghz.py
:start-after: # SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
```

[deutsch-jozsa]: https://en.wikipedia.org/wiki/Deutsch%E2%80%93Jozsa_algorithm
[examples-directory]: https://github.com/iqm-finland/QDMI-on-IQM/tree/main/examples
[ghz-state]: https://en.wikipedia.org/wiki/Greenberger%E2%80%93Horne%E2%80%93Zeilinger_state
[graph-state]: https://en.wikipedia.org/wiki/Graph_state
[measurement-based-quantum-computing]: https://en.wikipedia.org/wiki/Measurement-based_quantum_computation
[mqt-bench]: https://mqt.readthedocs.io/projects/bench/
[qiskit-nature]: https://qiskit-community.github.io/qiskit-nature/
[qsci]: https://arxiv.org/abs/2302.11320
[quantum-fourier-transform]: https://en.wikipedia.org/wiki/Quantum_Fourier_transform
[w-state]: https://en.wikipedia.org/wiki/W_state
