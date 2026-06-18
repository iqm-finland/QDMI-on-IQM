<p align="center">
  <a href="https://www.iqm.tech">
   <picture>
     <source media="(prefers-color-scheme: dark)" srcset="https://github.com/iqm-finland/QDMI-on-IQM/raw/refs/heads/main/docs/_static/iqm_logo_dark.svg">
     <img src="https://github.com/iqm-finland/QDMI-on-IQM/raw/refs/heads/main/docs/_static/iqm_logo.svg" alt="IQM Logo" width="40%">
   </picture>
  </a>
</p>

# QDMI on IQM

[![Core Apache 2.0 License](https://img.shields.io/static/v1?logo=Apache&label=Core&message=Apache%202.0&color=informational&style=flat-square)](https://opensource.org/licenses/Apache-2.0)
[![SPANK Plugin GPLv3 License](https://img.shields.io/static/v1?logo=gnu&label=SPANK&message=GPLv3&color=informational&style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![C++20](https://img.shields.io/static/v1?logo=cplusplus&label=C%2B%2B&message=20&color=informational&style=flat-square)](https://isocpp.org/)
[![CMake](https://img.shields.io/static/v1?logo=CMake&label=CMake&message=3.24%2B&color=informational&style=flat-square)](https://cmake.org/)

**QDMI on IQM** is IQM's official, production-ready implementation of the
[Quantum Device Management Interface (QDMI)](https://github.com/Munich-Quantum-Software-Stack/qdmi)—a
vendor-neutral C API for quantum hardware.
It connects HPC schedulers, middleware, and Python toolchains to IQM processors through a single,
stable interface, handling session management, calibration queries, and job lifecycle transparently.

<p align="center">
  <a href="https://iqm-finland.github.io/QDMI-on-IQM/">
  <img width=30% src="https://img.shields.io/badge/documentation-blue?style=for-the-badge&logo=read%20the%20docs" alt="Documentation" />
  </a>
</p>

## Installation

The easiest way to get started is via the Python package, which ships pre-compiled binaries for
Linux, macOS, and Windows:

```console
uv pip install iqm-qdmi          # core library and Python entry points
uv pip install iqm-qdmi[qiskit]  # adds the Qiskit backend (IQMBackend)
```

For C++ projects, integrate the library via CMake's `FetchContent` or install it through
[Spack](https://iqm-finland.github.io/QDMI-on-IQM/spack_guide.html) on HPC clusters.

## Where to Start

| I want to…                                                        | Guide                                                                           |
| :---------------------------------------------------------------- | :------------------------------------------------------------------------------ |
| Run circuits on IQM hardware from **Python / Qiskit**             | [Qiskit Integration](https://iqm-finland.github.io/QDMI-on-IQM/qiskit.html)     |
| Walk through end-to-end workloads (benchmarks, quantum chemistry) | [Examples](https://iqm-finland.github.io/QDMI-on-IQM/examples.html)             |
| Integrate the **C++ library** directly                            | [Usage Guide](https://iqm-finland.github.io/QDMI-on-IQM/usage.html)             |
| Install via **Spack** on an HPC cluster                           | [Spack Guide](https://iqm-finland.github.io/QDMI-on-IQM/spack_guide.html)       |
| Understand the Python package's entry points                      | [Python Package](https://iqm-finland.github.io/QDMI-on-IQM/python_package.html) |
| Contribute to the project                                         | [Contributing](https://iqm-finland.github.io/QDMI-on-IQM/contributing.html)     |

## Contributing

Contributions are welcome — bug reports, documentation improvements, and new features alike.
See the [Contributing Guide](https://iqm-finland.github.io/QDMI-on-IQM/contributing.html) for
the development workflow, coding standards, and pull request process.

## License

The components of this repository are licensed under two separate licenses:

- The main **QDMI-on-IQM** codebase (including the core C++ library and the Python wrapper) is licensed under the **Apache License 2.0 with LLVM exception**. See [LICENSE.md](LICENSE.md) for the license text.
- The **Slurm SPANK plugin** (located in the [spank/](spank/) directory) is licensed under the **GNU General Public License v3 (GPLv3)** because it links against GPL-licensed Slurm libraries. See [spank/LICENSE.md](spank/LICENSE.md) for the plugin license text.
