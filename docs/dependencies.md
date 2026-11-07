# Dependencies

[CMake dependencies] and [package metadata] define dependency versions and
bounds. `uv.lock` records the resolved Python environment.

## Runtime dependencies

CPR and nlohmann/json are compiled into the device library. curl is linked
statically on Linux and Windows; macOS uses the system library. QDMI supplies
interface headers, including the generated IQM declarations installed with the
development package; no QDMI driver library is linked into the device.

| Dependency      | Version                         | License                        | Purpose                        |
| :-------------- | :------------------------------ | :----------------------------- | :----------------------------- |
| [QDMI]          | 1.4.0                           | Apache-2.0 WITH LLVM-exception | Device interface and headers   |
| [nlohmann/json] | 3.12.0                          | MIT                            | JSON parsing and serialization |
| [CPR]           | 1.15.1                          | MIT                            | C++ HTTP client                |
| [curl]          | 8.23.0; system version on macOS | curl                           | HTTP transport                 |
| [OpenSSL]       | ≥3; Linux                       | Apache-2.0                     | TLS                            |

Linux wheels and SDK archives bundle OpenSSL; native builds use the installed
OpenSSL development package. Windows uses Schannel and macOS uses system curl's
TLS implementation. SDK archives include dependency notices under `licenses/`
and use the operating system runtime and CA trust store. Updating a bundled
library requires rebuilding the distribution.

## Optional Python integrations

The base package requires Python 3.11 or newer and has no Python runtime
dependencies. Python execution uses [MQT Core]'s QDMI client. Framework extras
install packages into the Python environment, not into the device wheel.

| Extra       | Dependencies                                       | Purpose                                                     |
| :---------- | :------------------------------------------------- | :---------------------------------------------------------- |
| `qiskit`    | MQT Core with Qiskit support and Qiskit Algorithms | Qiskit backend, primitives, and sampler/estimator utilities |
| `pennylane` | MQT Core with PennyLane support                    | Finite-shot QNodes                                          |

Qiskit and PennyLane can be installed independently. MQT Core's extras define
their supported versions; [package metadata] records all dependency bounds. See
[Qiskit](qiskit.md) and [PennyLane](pennylane.md) for installation and usage.
The native C/C++ library does not depend on MQT Core or a Python framework.

## Build dependencies

Native builds require a C++20 compiler, CMake 3.24 or newer, and Git. Linux
builds also require OpenSSL development headers. Python packaging uses
`scikit-build-core`; documentation uses Sphinx and Doxygen. The `build`, `test`,
and `docs` dependency groups install tools for their respective tasks. See
[contributing](contributing.md) for build and validation commands.

The [examples](examples.md) declare workload-specific dependencies in their
[PEP 723](https://peps.python.org/pep-0723/) script metadata. These packages are
not required by the device library.

[CMake dependencies]: https://github.com/iqm-finland/QDMI-on-IQM/blob/main/cmake/ExternalDependencies.cmake
[package metadata]: https://github.com/iqm-finland/QDMI-on-IQM/blob/main/pyproject.toml
[CPR]: https://github.com/libcpr/cpr
[curl]: https://curl.se/
[MQT Core]: https://mqt.readthedocs.io/projects/core/
[nlohmann/json]: https://github.com/nlohmann/json
[OpenSSL]: https://openssl-library.org/
[QDMI]: https://github.com/Munich-Quantum-Software-Stack/qdmi
