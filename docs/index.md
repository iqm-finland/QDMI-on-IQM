# QDMI on IQM

This project exposes IQM quantum computing hardware via the Quantum Device Management Interface ([QDMI](https://github.com/Munich-Quantum-Software-Stack/qdmi)) standard.
It is implemented in C++20 and builds on the IQM Server API.
A Python wrapper is provided for convenient distribution and consumption.

This documentation combines user guides with generated API references for both C++ and Python.

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
