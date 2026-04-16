# QDMI on IQM

**QDMI on IQM** is IQM's official, production-ready implementation of the [Quantum Device Management Interface (QDMI)](https://github.com/Munich-Quantum-Software-Stack/qdmi).

By providing this officially supported implementation, we empower HPC centers and middleware developers to embed IQM processors via a stable, vendor-agnostic standard.
This avoids the need for fragile, bespoke adapter chains and allows users to keep their familiar tools while operators maintain manageable workflows.

The library seamlessly wraps backend orchestration—spanning persistent session control, calibration queries, and job semantics via the [IQM Server API](https://resonance.meetiqm.com/docs/api-reference)—to connect higher-level software and schedulers to both our cloud-hosted endpoints through [IQM Resonance](https://resonance.meetiqm.com) and our on-premise device deployments.

The underlying library is written in C++20, providing a reliable foundation for long-lived integration code.
Pre-compiled binaries for major OS and architecture configurations are provided alongside an official Python wrapper for straightforward installation via `uv` and direct use in Python workflows.

This documentation combines [user guides](usage) with generated API references for both [C](capi/index) and [Python](api/iqm/qdmi/index).

```{toctree}
:maxdepth: 2
:caption: User Guide

usage
python_package
spack_guide
development_guide
dependencies
contributing
support
security
```

```{toctree}
:maxdepth: 3
:caption: Python API Reference

api/iqm/qdmi/index
```

```{toctree}
:glob:
:maxdepth: 2
:caption: C API Reference

cpp_api/index
```
