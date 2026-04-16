# Dependencies

This project relies on several third-party libraries and tools. Below is a comprehensive list of these dependencies along with their purposes, versions, and licenses.

## Core Runtime Dependencies

These dependencies are linked into the shared library and **shipped with every built wheel or binary**.

| Dependency      | Version          | License                        | Purpose                                       |
| :-------------- | :--------------- | :----------------------------- | :-------------------------------------------- |
| [QDMI]          | 1.2.x (@c5f71c6) | Apache-2.0 with LLVM-exception | QDMI specification and interface headers      |
| [nlohmann/json] | 3.12.0           | MIT License                    | JSON parsing and serialization                |
| [cURL]          | system-provided  | MIT/X derivative               | HTTP client library for backend communication |

[QDMI]: https://github.com/Munich-Quantum-Software-Stack/qdmi
[nlohmann/json]: https://github.com/nlohmann/json
[cURL]: https://github.com/curl/curl

```{note}
In pre-built wheels, libcurl and its transitive dependencies (OpenSSL, nghttp2, etc.) are automatically bundled by platform-specific repair tools: [`auditwheel`](https://github.com/pypa/auditwheel) on Linux, [`delocate`](https://github.com/matthew-brett/delocate) on macOS, and [`delvewheel`](https://github.com/adang1345/delvewheel) on Windows. On Linux, the exact bundled versions correspond to those available in the [`manylinux_2_28`](https://github.com/pypa/manylinux) container images used by [cibuildwheel](https://cibuildwheel.pypa.io/).
```

## Test Dependencies

Used for testing only, **not shipped** in any binary or wheel.

### C++ Tests

| Dependency   | Version | License      | Purpose                          |
| :----------- | :------ | :----------- | :------------------------------- |
| [googletest] | 1.17.0  | BSD-3-Clause | C++ unit and integration testing |

[googletest]: https://github.com/google/googletest

### Python Tests

| Dependency               | Version | License      | Purpose                         |
| :----------------------- | :------ | :----------- | :------------------------------ |
| [pytest]                 | ≥9.0.1  | MIT License  | Testing framework               |
| [pytest-console-scripts] | ≥1.4.1  | MIT License  | Testing CLI entry points        |
| [pytest-cov]             | ≥7.0.0  | MIT License  | Test coverage reporting         |
| [pytest-sugar]           | ≥1.1.1  | BSD-3-Clause | Prettier test output formatting |
| [pytest-xdist]           | ≥3.8.0  | MIT License  | Parallel test execution         |

[pytest]: https://github.com/pytest-dev/pytest
[pytest-console-scripts]: https://github.com/kvas-it/pytest-console-scripts
[pytest-cov]: https://github.com/pytest-dev/pytest-cov
[pytest-sugar]: https://github.com/Teemu/pytest-sugar
[pytest-xdist]: https://github.com/pytest-dev/pytest-xdist

## Documentation Dependencies

Used to generate the API documentation, **not shipped** in any binary or wheel.

| Dependency       | Version | License      | Purpose                      |
| :--------------- | :------ | :----------- | :--------------------------- |
| [Doxygen]        | 1.16.1  | GNU GPL v2   | API documentation generation |
| [Sphinx]         | ≥8.2.3  | BSD-2-Clause | Documentation site generator |
| [MyST-NB]        | ≥1.3.0  | BSD-3-Clause | Markdown + notebook pages    |
| [Breathe]        | ≥4.36.0 | BSD-2-Clause | Doxygen XML to Sphinx bridge |
| [Sphinx AutoAPI] | ≥3.6.1  | MIT License  | Python API reference pages   |

[Doxygen]: https://github.com/doxygen/doxygen
[Sphinx]: https://github.com/sphinx-doc/sphinx
[MyST-NB]: https://github.com/executablebooks/MyST-NB
[Breathe]: https://github.com/breathe-doc/breathe
[Sphinx AutoAPI]: https://github.com/readthedocs/sphinx-autoapi

```{note}
Doxygen is licensed under GNU GPL v2, but [documents produced by Doxygen are derivative works of the input, not of Doxygen itself](https://www.doxygen.nl/manual/), and are therefore not affected by the GPL. The generated documentation remains under the project's own license terms.
```
