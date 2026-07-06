# QDMI on IQM

**QDMI on IQM** is IQM's official, production-ready implementation of the
[Quantum Device Management Interface (QDMI)](https://github.com/Munich-Quantum-Software-Stack/qdmi).

By providing this officially supported implementation, IQM empowers HPC centers
and middleware developers to embed IQM processors via a stable, vendor-agnostic
standard—no fragile, bespoke adapter chains, just clean integration through a
shared interface.

The library is written in C++20 and communicates with IQM hardware through the
[IQM Server API](https://resonance.iqm.tech/docs/api-reference), handling
session control, calibration queries, and job lifecycle management
transparently. Pre-compiled binaries for all major platforms are provided
alongside a Python wrapper for straightforward installation via `pip` or `uv`.

## Where to Start

| I want to…                                                       | Start here                            |
| :--------------------------------------------------------------- | :------------------------------------ |
| Run quantum circuits on IQM hardware from **Python / Qiskit**    | [Qiskit Integration](qiskit.md)       |
| Run end-to-end example workloads (benchmarks, quantum chemistry) | [Examples](examples.md)               |
| Integrate the C++ library directly                               | [Usage Guide](usage.md)               |
| Integrate with **Slurm** on an HPC cluster                       | [SPANK Plugin Guide](spank_plugin.md) |
| Install via **Spack** on an HPC cluster                          | [Spack Guide](spack_guide.md)         |
| Understand the Python package and its entry points               | [Python Package](python_package.md)   |
| Contribute to the project                                        | [Contributing](contributing.md)       |

## API Reference

The generated API references cover both the C++ library and the Python package:
the {doc}`C++ API reference <capi/index>` documents all public QDMI functions
and types exposed by the library, while the
{doc}`C++ API reference <api/iqm/qdmi/index>` covers the `iqm-qdmi` package
including the Qiskit backend and its sampler and estimator primitives.

```{toctree}
:maxdepth: 1
:caption: User Guide

examples
qiskit
usage
python_package
spank_plugin

spack_guide

CHANGELOG
```

```{toctree}
:maxdepth: 1
:caption: Developer Guide

dependencies
contributing
support
security
```

```{toctree}
:maxdepth: 1
:caption: API Reference

capi/index
api/iqm/qdmi/index
```
