# Dependencies

This project relies on several third-party libraries and tools. Below is a
comprehensive list of these dependencies along with their purposes, versions,
and licenses.

## Core Runtime Dependencies

These dependencies are linked into the shared library and
**shipped with every built wheel or binary**.

| Dependency      | Version | License                        | Purpose                                                    |
| :-------------- | :------ | :----------------------------- | :--------------------------------------------------------- |
| [QDMI]          | 1.3.0   | Apache-2.0 with LLVM-exception | QDMI specification and interface headers                   |
| [nlohmann/json] | 3.12.0  | MIT License                    | JSON parsing and serialization                             |
| [CPR]           | 1.14.2  | MIT License                    | C++ Requests HTTP client library for backend communication |

```{note}
In pre-built wheels, libcurl and its transitive dependencies (OpenSSL, nghttp2,
etc.) are automatically bundled by platform-specific repair tools:
[`auditwheel`](https://github.com/pypa/auditwheel) on Linux,
[`delocate`](https://github.com/matthew-brett/delocate) on macOS, and
[`delvewheel`](https://github.com/adang1345/delvewheel) on Windows. On Linux,
the exact bundled versions correspond to those available in the
[`manylinux_2_28`](https://github.com/pypa/manylinux) container images used by
[cibuildwheel](https://cibuildwheel.pypa.io/).
```

## Build Dependencies

Used to build the Python package and bridge packaging with the CMake-based
native build. These are **not shipped** in any binary or wheel.

| Dependency          | Version | License    | Purpose                                    |
| :------------------ | :------ | :--------- | :----------------------------------------- |
| [scikit-build-core] | ≥1.0.1  | Apache-2.0 | Python build backend and CMake integration |

## Optional Python Integration Dependencies

These are installed when users request the `qiskit` extra via
`iqm-qdmi[qiskit]`. They live in the user's Python environment and are
**not bundled** inside the wheel itself.

| Dependency | Version | License     | Purpose                                                                  |
| :--------- | :------ | :---------- | :----------------------------------------------------------------------- |
| [MQT Core] | ~=3.7.0 | MIT License | QDMI-aware Qiskit provider, backend, sampler, and estimator integrations |
| [Qiskit]   | ≥1.1    | Apache-2.0  | Quantum circuit construction, transpilation, and primitive interfaces    |

## End-to-End Example Dependencies

On top of the Python integration dependencies, the example scripts rely on
additional libraries to construct and execute their workloads. The scripts in
`examples/` declare these dependencies in their own
[PEP 723](https://peps.python.org/pep-0723/) metadata. These dependencies are
resolved when the scripts are executed locally and are **not shipped** as part
of the `iqm-qdmi` wheel.

| Dependency      | Version | License     | Applies to         | Purpose                                         |
| :-------------- | :------ | :---------- | :----------------- | :---------------------------------------------- |
| [MQT Bench]     | ≥2.2.2  | MIT License | Benchmark examples | Standardized benchmark circuit generation       |
| [Qiskit Nature] | ≥0.7.2  | Apache-2.0  | QSCI example       | Electronic-structure modeling and qubit mapping |

## Test Dependencies

Used for testing only, **not shipped** in any binary or wheel.

### C++ Tests

| Dependency   | Version | License      | Purpose                          |
| :----------- | :------ | :----------- | :------------------------------- |
| [googletest] | 1.17.0  | BSD-3-Clause | C++ unit and integration testing |

### Python Tests

| Dependency               | Version | License      | Purpose                         |
| :----------------------- | :------ | :----------- | :------------------------------ |
| [pytest]                 | ≥9.0.1  | MIT License  | Testing framework               |
| [pytest-console-scripts] | ≥1.4.1  | MIT License  | Testing CLI entry points        |
| [pytest-cov]             | ≥7.0.0  | MIT License  | Test coverage reporting         |
| [pytest-sugar]           | ≥1.1.1  | BSD-3-Clause | Prettier test output formatting |
| [pytest-xdist]           | ≥3.8.0  | MIT License  | Parallel test execution         |

## Documentation Dependencies

Used to generate the documentation site and API reference, **not shipped** in
any binary or wheel.

| Dependency          | Version    | License      | Purpose                                                 |
| :------------------ | :--------- | :----------- | :------------------------------------------------------ |
| [Doxygen]           | 1.16.1     | GNU GPL v2   | API documentation generation                            |
| [Sphinx]            | ≥8.1.3     | BSD-2-Clause | Documentation site generator                            |
| [MyST-NB]           | ≥1.3.0     | BSD-3-Clause | Markdown and notebook pages                             |
| [Breathe]           | ≥4.36.0    | BSD License  | Doxygen XML to Sphinx bridge                            |
| [Sphinx AutoAPI]    | ≥3.6.1     | MIT License  | Python API reference pages                              |
| [Furo]              | ≥2025.9.25 | MIT License  | Sphinx HTML theme                                       |
| [Sphinx Copybutton] | ≥0.5.2     | MIT License  | Copy-to-clipboard buttons for code blocks               |
| [Sphinx Design]     | ≥0.6.1     | MIT License  | Layout and UI directives such as cards, grids, and tabs |

```{note}
Doxygen is licensed under GNU GPL v2, but
[documents produced by Doxygen are derivative works of the input, not of Doxygen itself](https://www.doxygen.nl/manual/),
and are therefore not affected by the GPL. The generated documentation remains
under the project's own license terms.
```

## Development Dependencies

Used for local development workflows, **not shipped** in any binary or wheel.

| Dependency | Version    | License     | Purpose                                                          |
| :--------- | :--------- | :---------- | :--------------------------------------------------------------- |
| [nox]      | ≥2026.4.10 | Apache-2.0  | Session runner for tests, linting, docs, packaging, and examples |
| [ty]       | ==0.0.35   | MIT License | Static type checking                                             |

[Breathe]: https://github.com/breathe-doc/breathe
[CPR]: https://github.com/libcpr/cpr
[Doxygen]: https://github.com/doxygen/doxygen
[Furo]: https://github.com/pradyunsg/furo
[googletest]: https://github.com/google/googletest
[MQT Bench]: https://mqt.readthedocs.io/projects/bench/
[MQT Core]: https://mqt.readthedocs.io/projects/core/
[MyST-NB]: https://github.com/executablebooks/MyST-NB
[nlohmann/json]: https://github.com/nlohmann/json
[nox]: https://github.com/wntrblm/nox
[pytest]: https://github.com/pytest-dev/pytest
[pytest-console-scripts]: https://github.com/kvas-it/pytest-console-scripts
[pytest-cov]: https://github.com/pytest-dev/pytest-cov
[pytest-sugar]: https://github.com/Teemu/pytest-sugar
[pytest-xdist]: https://github.com/pytest-dev/pytest-xdist
[QDMI]: https://github.com/Munich-Quantum-Software-Stack/qdmi
[Qiskit]: https://github.com/Qiskit/qiskit
[Qiskit Nature]: https://github.com/qiskit-community/qiskit-nature
[scikit-build-core]: https://github.com/scikit-build/scikit-build-core
[Sphinx]: https://github.com/sphinx-doc/sphinx
[Sphinx AutoAPI]: https://github.com/readthedocs/sphinx-autoapi
[Sphinx Copybutton]: https://github.com/executablebooks/sphinx-copybutton
[Sphinx Design]: https://github.com/executablebooks/sphinx-design
[ty]: https://github.com/astral-sh/ty
