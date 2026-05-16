# End-User Examples

This page documents the standalone Qiskit example scripts that live under `examples/`.
Unlike the thin regression tests in `test/python/`, these scripts are end-user demonstrations built on top of the packaged {py:class}`~iqm.qdmi.qiskit.IQMBackend` and the generic QDMI backend surface exposed by MQT Core.

The examples tree contains:

- `examples/deutsch_jozsa.py`
- `examples/ghz.py`
- `examples/graphstate.py`
- `examples/qft.py`
- `examples/wstate.py`
- `examples/qsci_h2.py`
- `examples/qsci_postprocess.py`

## Running the Examples

Run the scripts from the repository root.
The `iqm-qdmi` CLI can launch a local example script through `uv`, so the shortest repository-local commands are:

The IQM-backed path uses the same environment-variable contract as the rest of the package:

- `IQM_BASE_URL`: IQM server endpoint.
- `IQM_TOKEN` or `RESONANCE_API_KEY`: authentication token.
- `IQM_TOKENS_FILE`: optional authentication file.
- `IQM_QC_ALIAS` or `IQM_QC_ID`: optional explicit target selection.

For quick simulator-backed runs, pass `--backend sim` on the command line instead of setting a dedicated examples environment variable.

```console
$ uvx nox -s examples

$ uvx --from . iqm-qdmi examples/ghz.py --backend sim --shots 128

$ export IQM_BASE_URL="https://desired-iqm-server.com"
$ export RESONANCE_API_KEY="your-api-key"
$ uvx --from . iqm-qdmi examples/qsci_h2.py --backend iqm
```

:::note
The QSCI example depends on PySCF, which is [not supported on Windows](https://pyscf.org/user/install.html).
:::

:::{important}
The IQM-backed example thresholds are tuned for real IQM QPUs.
Mock IQM targets selected through `IQM_QC_ALIAS` or `IQM_QC_ID` are still accepted, but some stricter distribution and energy checks may fail on them.
Use `--backend sim` if you want a simulator-backed validation path without IQM credentials.
:::

## MQT Bench Scripts

[MQT Bench](https://mqt.readthedocs.io/projects/bench/) provides a catalog of benchmark circuits.
In this repository, the standalone MQT Bench scripts demonstrate sampler-based end-user flows by generating benchmark circuits, transpiling them for the selected backend, executing them through {py:class}`~mqt.core.plugins.qiskit.sampler.QDMISampler`, and validating the observed bitstring distribution.

The current scripts cover:

- GHZ states
- Deutsch-Jozsa
- Quantum Fourier Transform
- Graph states
- W-states

Each script exposes a small CLI surface, including `--backend`, `--shots`, and a benchmark-specific validation threshold such as `--threshold`, `--max-zero-probability`, or `--min-states`.
The simulator path uses `--backend sim` and applies a small explicit gate basis for stable transpilation against the QDMI simulator backend.

## QSCI Script

The **Quantum-Selected Configuration Interaction (QSCI)** example combines a variational quantum step with classical postprocessing to estimate a molecular ground-state energy for H2.
The implementation is split across `examples/qsci_h2.py` and the helper module `examples/qsci_postprocess.py`.

The `qsci_h2.py` script performs the following steps:

1. Build the electronic-structure problem with Qiskit Nature.
2. Construct a {py:class}`~qiskit_nature.second_q.circuit.library.UCCSD` ansatz and map the Hamiltonian with {py:class}`~qiskit_nature.second_q.mappers.JordanWignerMapper`.
3. Optimize the ansatz with Qiskit's {py:class}`~qiskit.primitives.BackendEstimator` so {py:class}`~qiskit_algorithms.minimum_eigensolvers.VQE` can use the estimator V1 interface it still expects.
4. Sample the trained ansatz with {py:class}`~mqt.core.plugins.qiskit.sampler.QDMISampler`.
5. Postprocess the measured configurations classically and compare the resulting energy against the exact reference.

The script exposes CLI knobs for `--shots`, `--maxiter`, `--cutoff`, and `--energy-tolerance` so you can trade runtime for precision.
The defaults are tuned for example runs rather than aggressive convergence.

## Relationship to the Thin Python Tests

The lightweight tests in `test/python/test_qiskit_backend.py` remain the fast regression layer for the backend itself.
They verify creation plus direct {py:meth}`~mqt.core.plugins.qiskit.backend.QDMIBackend.run`, {py:meth}`~iqm.qdmi.qiskit.IQMBackend.sampler`, and {py:meth}`~iqm.qdmi.qiskit.IQMBackend.estimator` calls.

The standalone scripts in `examples/` answer a different question: what do realistic application-level workflows look like when they utilize the backend?
The dedicated `examples` nox session provides a thin automation layer for running those scripts on the simulator in CI and during local development.
