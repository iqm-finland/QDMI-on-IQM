# Development Guide

Ready to contribute to the project? This guide will get you started.

## Initial Setup

1. Get the code

   **External Contribution**

   If you do not have write access to the [iqm-finland/QDMI-on-IQM](https://github.com/iqm-finland/QDMI-on-IQM) repository, fork the repository on GitHub (see <https://docs.github.com/en/get-started/quickstart/fork-a-repo>) and clone your
   fork locally.

   ```bash
   $ git clone git@github.com:your_name_here/QDMI-on-IQM.git
   ```

   **Internal Contribution**

   If you do have write access to the [iqm-finland/QDMI-on-IQM](https://github.com/iqm-finland/QDMI-on-IQM) repository, clone the repository locally.

   ```bash
   $ git clone git@github.com/iqm-finland/QDMI-on-IQM.git
   ```

2. Change into the project directory

   ```bash
   $ cd QDMI-on-IQM
   ```

3. Create a branch for local development

   ```bash
   $ git checkout -b name-of-your-bugfix-or-feature
   ```

   Now you can make your changes locally.

4. **Install pre-commit hooks** (recommended)

   The project uses [pre-commit](https://pre-commit.com/) hooks for running linters and formatting tools on each commit. These checks can be run automatically on every commit via [prek](https://prek.j178.dev) (recommended).

   To set this up, install [prek](https://prek.j178.dev/), e.g., via:

   **macOS and Linux:**

   ```console
   $ curl --proto '=https' --tlsv1.2 -LsSf https://github.com/j178/prek/releases/latest/download/prek-installer.sh | sh
   ```

   **Windows:**

   ```console
   $ powershell -ExecutionPolicy ByPass -c "irm https://github.com/j178/prek/releases/latest/download/prek-installer.ps1 | iex"
   ```

   **Using uv:**

   ```console
   $ uv tool install prek
   ```

   Then run:

   ```console
   $ prek install
   ```

## Working on the C++ library

Building the project requires a C++ compiler supporting _C++20_ and CMake with a minimum version of _3.24_.

### Running workflows with nox

The repository includes [nox](https://nox.thea.codes/) sessions for common development workflows.
They are especially convenient for the Python-side checks and for building docs.

**Run linting checks (same checks as prek):**

```console
$ uvx nox -s lint
```

**Run Python tests across configured Python versions:**

```console
$ uvx nox -s tests
```

To run tests only for one Python version:

```console
$ uvx nox -s tests-3.12
```

**Build documentation via CMake/Doxygen through nox:**

```console
$ uvx nox -s docs
```

### Configure and Build

This project uses _CMake_ as the main build configuration tool.
Building a project using CMake is a two-stage process.
First, CMake needs to be _configured_ by calling

```console
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_QDMI_TESTS=ON
```

This tells CMake to

- search the current directory `.` (passed via `-S`) for a `CMakeLists.txt` file.
- process it into a directory `build` (passed via `-B`).
- the flag `-DCMAKE_BUILD_TYPE=Release` tells CMake to configure a _Release_ build (as opposed to, e.g., a _Debug_ build).
- the flag `-DBUILD_IQM_QDMI_TESTS=ON` enables building the tests.
- the flag `-DIQM_QDMI_COVERAGE=ON` can be added to enable code coverage support (see below).
- the flag `-DBUILD_IQM_QDMI_DOCS=ON` can be added to enable building the documentation (see below).

After configuring with CMake, the project can be built by calling

```console
$ cmake --build build --config Release
```

This tries to build the project in the `build` directory (passed via `--build`).
Some operating systems and development environments explicitly require a configuration to be set, which is why the `--config` flag is also passed to the build command.
The flag `--parallel <NUMBER_OF_THREADS>` may be added to trigger a parallel build.

Building the project this way generates

- the main project libraries in the `build/src` directory
- some test executables in the `build/test` directory

### C++ Testing and Code Coverage

We use the [GoogleTest](https://google.github.io/googletest/primer.html) framework for testing the C++ library.
The tests are located in the `test` directory and are separated into two categories:

- **Unit Tests:** These are self-contained tests that do not require any external dependencies or a live connection to an endpoint. They are located in the `test/unit` directory.
- **Integration Tests:** These tests require a live connection to an IQM backend and are located in the `test/integration` directory.

A corresponding CI pipeline on GitHub also runs the tests and checks for any failures.

To run the tests, you can use `ctest` after building the project (as described above).

**Running all tests:**

```console
$ ctest -C Release --test-dir build --output-on-failure
```

**Running only the unit tests:**

```console
$ ctest -C Release --test-dir build/test/unit --output-on-failure
```

**Running only the integration tests:**

Before running the integration tests, make sure you have set the necessary environment variables, such as `IQM_BASE_URL` and `RESONANCE_API_KEY`.

```console
$ ctest -C Release --test-dir build/test/integration --output-on-failure
```

**Running only install/public API tests:**

The `install-test` setup in CI validates only the installed public API surface.
It intentionally excludes tests that depend on private/internal symbols.

```console
$ ctest -C Release --test-dir build -L install-public --output-on-failure
```

To generate a code coverage report, you can configure the project with `-DIQM_QDMI_COVERAGE=ON` and then run `lcov` in the build directory.

### C++ Code Formatting and Linting

This project mostly follows the [LLVM Coding Standard](https://llvm.org/docs/CodingStandards.html), which is a set of guidelines for writing C++ code.
To ensure the quality of the code and that it conforms to these guidelines, we use

- [`clang-tidy`](https://clang.llvm.org/extra/clang-tidy/) -- a static analysis tool that checks for common mistakes in C++ code, and
- [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html) -- a tool that automatically formats C++ code according to a given style guide.

Our `pre-commit` configuration also includes `clang-format`.
If you have installed `prek`, it will automatically run `clang-format` on your code before each commit.

Our CI pipeline will run clang-tidy over the changes in your pull request and report any issues it finds.

### C++ Documentation

We expect any new additions to the code base to be documented using [Doxygen](https://www.doxygen.nl/index.html) comments.
When touching existing code, we encourage you to add Doxygen comments to the code you touch or refactor.

For some tips on how to write good Doxygen comments, see the [Doxygen Manual](https://www.doxygen.nl/manual/docblocks.html).

## Working on the Documentation

The documentation is written in Markdown and built using [Doxygen](https://www.doxygen.nl/index.html).
The documentation source files can be found in the `docs/` directory.

To build the documentation locally, you need to have Doxygen installed.
Then, after configuring the project with CMake and the `-DBUILD_IQM_QDMI_DOCS=ON` flag, you can build the documentation by running:

```console
$ cmake --build build --target iqm_qdmi_device_docs
```

The documentation will be generated in the `build/docs` directory as both HTML and LaTeX.
You can view the HTML documentation by opening the `index.html` file in a web browser.

## IQM API Usage in QDMI Device Implementation

This section describes how the unified IQM Server API, as defined in `iqm_api_config.hpp` and `iqm_api_config.cpp`, is used to provide the functionality of the QDMI device implementation in `iqm_device.cpp`.

### API Endpoints Used

The QDMI device implementation uses the following endpoints from the unified IQM Server API:

**Quantum Computer Management:**

- `GET_QUANTUM_COMPUTERS`: Retrieves the list of available quantum computers with their IDs and aliases.
- `GET_STATIC_QUANTUM_ARCHITECTURE`: Fetches the static quantum architecture (qubits and connectivity) for a specific quantum computer.
- `GET_DYNAMIC_QUANTUM_ARCHITECTURE`: Obtains the set of calibrated gates and their implementations for a given calibration set.
- `GET_CALIBRATION_SET_QUALITY_METRICS`: Gets calibration metrics such as qubit T1/T2 times and gate fidelities.

**Job Management:**

- `SUBMIT_CIRCUIT_JOB`: Submits a quantum circuit job (QIR or IQM JSON format) for execution.
- `GET_JOB_STATUS`: Checks the status of a submitted job.
- `GET_JOB_ARTIFACT_MEASUREMENT_COUNTS`: Retrieves the measurement results as aggregated histogram counts of a completed job.
- `GET_JOB_ARTIFACT_MEASUREMENTS`: Retrieves the individual shot measurement results of a completed job.
- `CANCEL_JOB`: Cancels a running job.

**Calibration Jobs (if supported):**

- `COCOS_HEALTH`: Health check endpoint to determine if calibration jobs are supported.
- `SUBMIT_CALIBRATION_JOB`: Submits a calibration job for execution.
- `GET_CALIBRATION_JOB_STATUS`: Checks the status of a calibration job.
- `ABORT_CALIBRATION_JOB`: Cancels a running calibration job.

### When API Calls Happen

**During Session Initialization (`IQM_QDMI_device_session_init`):**

1. `GET_QUANTUM_COMPUTERS`: Fetches the list of available quantum computers.
2. The appropriate quantum computer is selected based on ID, alias, or first available.
3. `GET_STATIC_QUANTUM_ARCHITECTURE`: Retrieves the static architecture (qubits, connectivity).
4. `GET_DYNAMIC_QUANTUM_ARCHITECTURE`: Fetches calibrated gates for the default calibration set.
5. `GET_CALIBRATION_SET_QUALITY_METRICS`: Retrieves quality metrics (T1, T2, fidelities) if available.
6. `COCOS_HEALTH`: Checks whether calibration jobs are supported.

**During Job Submission and Management:**

- `SUBMIT_CIRCUIT_JOB` or `SUBMIT_CALIBRATION_JOB`: Called when `IQM_QDMI_device_job_submit` is invoked.
- `GET_JOB_STATUS` or `GET_CALIBRATION_JOB_STATUS`: Polled when `IQM_QDMI_device_job_check` or `IQM_QDMI_device_job_wait` is called.
- `GET_JOB_ARTIFACT_MEASUREMENT_COUNTS`: Fetched when histogram results are queried.
- `GET_JOB_ARTIFACT_MEASUREMENTS`: Fetched when individual shot results are queried.
- `CANCEL_JOB` or `ABORT_CALIBRATION_JOB`: Called when `IQM_QDMI_device_job_cancel` is invoked.

**After Calibration Job Completion:**
When querying results of a calibration job, the implementation automatically:

1. Extracts the new calibration set ID from the job result.
2. Calls `GET_DYNAMIC_QUANTUM_ARCHITECTURE` with the new calibration set ID.
3. Calls `GET_CALIBRATION_SET_QUALITY_METRICS` to update quality metrics.

### API Configuration

The `APIConfig` class in `iqm_api_config.hpp` provides a centralized way to build URLs for API endpoints. The base URL is set during session initialization, and endpoints are constructed using string formatting with parameters like quantum computer alias, calibration set ID, and job ID.

### Error Handling

- If an API call fails, the HTTP client returns an appropriate QDMI status code.
- Error messages are logged using the logging system.
- For calibration job endpoints, if the server doesn't support them (e.g., `COCOS_HEALTH` check fails), the `supports_calibration_jobs_` flag is set to false, and calibration job submissions will return `QDMI_ERROR_NOTSUPPORTED`.

### API Response Expectations

The implementation expects JSON responses in specific formats:

- **Quantum Computers List**: Array of objects with `id` and `alias` fields.
- **Static Architecture**: Array with a single object containing `qubits` (array of strings) and `connectivity` (array of 2-element arrays).
- **Dynamic Architecture**: Object with `calibration_set_id` (string) and `gates` (nested objects with gate details).
- **Quality Metrics**: Object with `observations` array containing `dut_field`, `value`, and `invalid` fields.
- **Job Status**: Object with job status, errors, and messages.
- **Measurement Counts Artifact**: Array with a single object containing `counts` (object mapping bitstrings to count integers).
- **Measurements Artifact**: Array typically containing a single object where each measurement key maps to an array of all shot results. Each shot result is a single-element array containing an integer (0 or 1). For example: `[{"meas_2_0_0": [[0], [1], [0], [1]], "meas_2_0_1": [[1], [0], [1], [0]]}]` represents 4 shots measuring two qubits, where each measurement key contains all results for that qubit across all shots.

For more details, see the implementation in `iqm_device.cpp` and the API configuration in `iqm_api_config.hpp`/`.cpp`.
