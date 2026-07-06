---
file_format: mystnb
kernelspec:
  name: python3
mystnb:
  number_source_lines: true
---

# Running End-to-End Quantum Workloads on IQM Hardware via QDMI-on-IQM

Welcome to the end-to-end tutorial. This guide walks you step by step through
driving real quantum workloads on IQM systems using QDMI-on-IQM and the packaged
{py:class}`~iqm.qdmi.qiskit.IQMBackend`.

Whether you want to estimate molecular ground-state energies with [QSCI][qsci]
or benchmark hardware with [MQT Bench][mqt-bench], the example scripts in this
repository provide a practical starting point. This tutorial focuses on two
application areas:

- **Quantum chemistry:** using [QSCI][qsci] and [Qiskit Nature][qiskit-nature]
  to estimate the ground-state energy of an H2 molecule.
- **Benchmarking:** running [MQT Bench][mqt-bench] programs such as [GHZ
  states][ghz-state], [Deutsch-Jozsa][deutsch-jozsa],
  [QFT][quantum-fourier-transform], [graph states][graph-state], [W
  states][w-state], [Grover][grover], or [Quantum Phase Estimation][qpe].

:::{important}
The example scripts live in the QDMI-on-IQM repository and are not shipped with
the distribution of the [`iqm-qdmi` Python package](python_package.md) on PyPI.
Hence, you need to download or clone the repository to access them. You can do
so by running the following command in your terminal:

```console
git clone https://github.com/iqm-finland/QDMI-on-IQM.git
```

:::

## Configure Your Environment

The IQM-backed path relies on a small environment-variable contract to
authenticate and route your jobs. Before running any of the examples, make sure
the following variables are set as needed:

- `IQM_BASE_URL`: The endpoint of the IQM server you are targeting (e.g.,
  `https://resonance.iqm.tech` for IQM Resonance).
- `IQM_TOKEN`: Your authentication token.
- `IQM_QC_ALIAS`: Optional explicit selection of the target quantum computer.

For the full set of authentication options available when configuring C++
sessions directly, see [Authentication Methods](usage.md#authentication-methods)
in the Usage Guide.

You can either run the full suite of examples using the dedicated `nox` session
or individually execute the scripts from the command line.

```console
# Run the entire suite
uvx nox -s examples

# Run specific examples
./examples/qsci_h2.py --shots 256 --maxiter 5 --cutoff 4
./examples/mqt_bench.py --benchmark ghz --shots 128
```

## Quantum Chemistry

Our first workload is a quantum chemistry application that estimates the
ground-state energy of a hydrogen molecule. The script `examples/qsci_h2.py`
follows a [Quantum-Selected Configuration Interaction][qsci]-style workflow: a
hybrid quantum-classical approach that combines variational optimization,
circuit sampling, and classical diagonalization in a reduced subspace. To build
the chemistry problem, it leans on [Qiskit Nature][qiskit-nature] to map an
electronic-structure model to qubit operators.

By running `examples/qsci_h2.py`, you will:

1. Build an electronic-structure problem for an H2 molecule.
2. Map the physical system to qubits with [Qiskit Nature][qiskit-nature].
3. Optimize a UCCSD ansatz against an IQM backend.
4. Sample the circuit to gather bitstrings.
5. Reconstruct the energy estimate.

The script prints an execution trace along the way, showing the progress of the
demonstration. QSCI is a great first end-to-end workload beyond a simple toy
circuit.

:::{note}
The QSCI example depends on PySCF for classical chemistry calculations, and
[PySCF is not supported on Windows](https://pyscf.org/user/install.html).
:::

## MQT Bench Programs

To understand how the backend behaves on standard programs, we move on to [MQT
Bench][mqt-bench]. MQT Bench is an open-source benchmark suite that collects
representative quantum algorithms across several abstraction levels. In this
repository, the benchmark scripts show how to generate those programs, transpile
them for the selected target, execute them through
{py:class}`~mqt.core.plugins.qiskit.sampler.QDMISampler`, and inspect the
resulting bitstring distributions.

The `examples/mqt_bench.py` entrypoint currently covers the following
algorithms:

- `ghz`: Prepares a [GHZ state][ghz-state], a highly entangled state that serves
  as a strong baseline test for multi-qubit entanglement fidelity.
- `dj`: Implements the [Deutsch-Jozsa algorithm][deutsch-jozsa], one of the
  earliest examples of a quantum algorithm with an exponential query advantage
  over its classical counterpart.
- `qft`: Computes the [Quantum Fourier Transform][quantum-fourier-transform], a
  central building block used in algorithms such as phase estimation and Shor's
  algorithm.
- `graphstate`: Generates [graph states][graph-state], an important family of
  entangled states that also serve as key resources for [measurement-based
  quantum computing][measurement-based-quantum-computing].
- `wstate`: Creates a [W state][w-state], a multipartite entangled state that
  retains pairwise entanglement even if one qubit is lost.
- `grover`: Implements [Grover's algorithm][grover], a quantum search algorithm
  that provides a quadratic speedup for unstructured search problems.
- `qpe`: Implements the [Quantum Phase Estimation algorithm][qpe], a fundamental
  algorithm that estimates the eigenvalues of a unitary operator and underpins
  many quantum algorithms, including Shor's factoring algorithm.

The benchmark entrypoint exposes a compact, consistent CLI:

- `--benchmark`: Selects the benchmark family to run.
- `--backend`: Selects `iqm` for hardware runs or `sim` for simulator runs.
- `--shots`: Controls how many samples are collected from the executed circuit.
- `--num-qubits`: Adjusts the problem size for the benchmark families that
  support it.

### Code Example: Preparing and Sampling from a GHZ State

The following snippet shows the benchmark runner in full. The same architecture
covers every supported benchmark family; only the selected benchmark and
validation logic differ.

```{literalinclude} ../examples/mqt_bench.py
:language: python
:caption: examples/mqt_bench.py
:start-after: "# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception"
```

Execution on a simulator backend should yield a near-perfect distribution of the
expected bitstrings (all 0s and all 1s for the GHZ state):

```{code-cell} ipython3
!../examples/mqt_bench.py --benchmark ghz --backend sim --shots 8192 --num-qubits 20
```

Now try running the same script with `--backend iqm` to see how the distribution
looks on real hardware. Remember to set the required environment variables for
authentication before running the script.

[deutsch-jozsa]: https://en.wikipedia.org/wiki/Deutsch%E2%80%93Jozsa_algorithm
[ghz-state]: https://en.wikipedia.org/wiki/Greenberger%E2%80%93Horne%E2%80%93Zeilinger_state
[graph-state]: https://en.wikipedia.org/wiki/Graph_state
[measurement-based-quantum-computing]: https://en.wikipedia.org/wiki/Measurement-based_quantum_computation
[mqt-bench]: https://mqt.readthedocs.io/projects/bench/
[qiskit-nature]: https://qiskit-community.github.io/qiskit-nature/
[qsci]: https://arxiv.org/abs/2302.11320
[quantum-fourier-transform]: https://en.wikipedia.org/wiki/Quantum_Fourier_transform
[w-state]: https://en.wikipedia.org/wiki/W_state
[grover]: https://en.wikipedia.org/wiki/Grover%27s_algorithm
[qpe]: https://en.wikipedia.org/wiki/Quantum_phase_estimation_algorithm
