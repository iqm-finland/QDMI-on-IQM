# End-User Examples

The examples in this repository show how to drive real workloads on IQM systems through the packaged {py:class}`~iqm.qdmi.qiskit.IQMBackend`.

This page focuses on two families of examples:

- **Quantum chemistry:** QSCI for an H2 ground-state workflow.
- **Benchmarks:** MQT Bench circuits such as GHZ, Deutsch-Jozsa, QFT, graph states, and W-states.

## Choose Your Setup

The IQM-backed path uses the same environment-variable contract as the rest of the package:

- `IQM_BASE_URL`: IQM server endpoint.
- `IQM_TOKEN` or `RESONANCE_API_KEY`: authentication token.
- `IQM_TOKENS_FILE`: optional authentication file.
- `IQM_QC_ALIAS` or `IQM_QC_ID`: optional explicit target selection.

### If You Installed `iqm-qdmi` From PyPI

Install the package with the Qiskit integration enabled:

```console
$ uv pip install "iqm-qdmi[qiskit]"
```

The example scripts themselves are not part of the wheel, so to run them, download or clone the [`examples/`](https://github.com/iqm-finland/QDMI-on-IQM/tree/main/examples) directory.
Once you have that directory locally, run an example with `uv run --script` so the script's embedded dependency metadata is honored:

```console
$ uv run --script examples/qsci_h2.py --backend sim --shots 256 --maxiter 5 --cutoff 4
$ uv run --script examples/ghz.py --backend sim --shots 128
```

If you want to target IQM hardware instead of the simulator, set the environment variables above and switch `--backend sim` to `--backend iqm`.

### If You Cloned the Repository

From a local checkout you can either run the whole examples suite or launch a single example directly through the local `iqm-qdmi` CLI:

```console
$ uvx nox -s examples

$ uvx --from . iqm-qdmi examples/qsci_h2.py --shots 256 --maxiter 5 --cutoff 4
$ uvx --from . iqm-qdmi examples/ghz.py --shots 128
```

For quick simulator-backed runs, pass `--backend sim` on the command line.
For IQM-backed runs, set the environment variables above and use `--backend iqm`.

## Quantum Chemistry: QSCI for H2

If you want a workflow that looks like an application instead of a toy circuit, start with QSCI.
This example combines Qiskit Nature, a UCCSD ansatz, IQM-backed estimation and sampling, and classical postprocessing to recover an H2 ground-state energy estimate and compare it against an exact reference.

In practice, the script shows you how to:

1. Build an electronic-structure problem for a small molecule.
2. Map it to qubits with Qiskit Nature.
3. Optimize a variational ansatz against an IQM backend or simulator.
4. Sample the trained circuit.
5. Classically reconstruct an energy estimate from the measured bitstrings.

The classical postprocessing helper used by this example lives in `examples/qsci_postprocess.py`.

```{literalinclude} ../examples/qsci_h2.py
:language: python
:caption: examples/qsci_h2.py
:start-after: # SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
```

:::note
The QSCI example depends on PySCF, which is [not supported on Windows](https://pyscf.org/user/install.html).
:::

## Benchmarks: MQT Bench on IQM Backends

[MQT Bench](https://mqt.readthedocs.io/projects/bench/) provides a catalog of benchmark circuits.
In this repository, the MQT Bench examples show how to generate benchmark circuits, transpile them for the selected backend, execute them through {py:class}`~mqt.core.plugins.qiskit.sampler.QDMISampler`, and inspect the observed bitstring distribution.

The current scripts cover:

- `examples/ghz.py`
- `examples/deutsch_jozsa.py`
- `examples/qft.py`
- `examples/graphstate.py`
- `examples/wstate.py`

Each script exposes a small CLI surface, including `--backend`, `--shots`, and usually `--num-qubits`.
The simulator path uses `--backend sim` and applies a small explicit gate basis for stable transpilation against the QDMI simulator backend.

Here is the GHZ example in full.
The same structure carries over to the other MQT Bench scripts, with benchmark-specific circuits and summaries swapped in where appropriate.

```{literalinclude} ../examples/ghz.py
:language: python
:caption: examples/ghz.py
:start-after: # SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
```
