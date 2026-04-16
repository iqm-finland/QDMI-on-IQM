<p align="center">
  <a href="https://www.meetiqm.com">
   <picture>
     <source media="(prefers-color-scheme: dark)" srcset="docs/_static/iqm_logo_dark.svg">
     <img src="docs/_static/iqm_logo.svg" alt="IQM Logo" width="40%">
   </picture>
  </a>
</p>

# QDMI on IQM

[![Apache 2.0 License](https://img.shields.io/static/v1?logo=Apache&label=License&message=Apache%202.0&color=informational&style=flat-square)](https://opensource.org/licenses/Apache-2.0)
[![C++20](https://img.shields.io/static/v1?logo=cplusplus&label=C%2B%2B&message=20&color=informational&style=flat-square)](https://isocpp.org/)
[![CMake](https://img.shields.io/static/v1?logo=CMake&label=CMake&message=3.24%2B&color=informational&style=flat-square)](https://cmake.org/)

A C++20 library that implements
the [QDMI (Quantum Device Management Interface)](https://github.com/Munich-Quantum-Software-Stack/qdmi) for IQM's
quantum computers, enabling seamless communication with IQM quantum hardware through a standardized interface.

## What is QDMI?

QDMI (Quantum Device Management Interface) is a vendor-neutral, standardized C API for interacting with quantum
computing hardware. This library provides an implementation of the QDMI specification tailored to IQM quantum computers,
allowing you to submit quantum circuits, query device properties, manage calibration, and retrieve results through a
unified interface.

## Why Use This Library?

- **Standardized Interface**: Use the QDMI standard to interact with IQM hardware
- **Modern C++20**: Leverage modern C++ features with a clean, type-safe implementation
- **Comprehensive Device Queries**: Access qubit properties, gate fidelities, coherence times, and more
- **Multiple Program Formats**: Submit circuits as QIR, IQM JSON; with additional support for calibration configurations

## Quick Start

### Prerequisites

- **C++ Compiler**: Supporting C++20
- **CMake**: Version 3.24 or higher
- **libcurl**: For HTTP communication
- **Access**: IQM quantum computer credentials (API key or tokens file)

### Installation

```console
# Install dependencies (Ubuntu/Debian)
$ sudo apt-get install cmake g++ libcurl4-openssl-dev

# Clone the repository
$ git clone https://github.com/iqm-finland/QDMI-on-IQM.git
$ cd QDMI-on-IQM

# Build the library
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
```

### Example: Bell State Circuit

```cpp
#include <iqm_qdmi/device.h>
#include <iostream>

int main() {
    IQM_QDMI_device_initialize();

    IQM_QDMI_Device_Session session;
    IQM_QDMI_device_session_alloc(&session);

    // Configure server and auth
    const char* url = "https://resonance.meetiqm.com";
    const char* token = "your-api-token";
    IQM_QDMI_device_session_set_parameter(session,
        QDMI_DEVICE_SESSION_PARAMETER_BASEURL, strlen(url) + 1, url);
    IQM_QDMI_device_session_set_parameter(session,
        QDMI_DEVICE_SESSION_PARAMETER_TOKEN, strlen(token) + 1, token);

    IQM_QDMI_device_session_init(session);

    // Submit Bell state circuit
    const char* circuit = R"({
        "name": "bell_state",
        "instructions": [
            {"name": "prx", "locus": ["QB1"], "args": {"angle_t": 0.5, "phase_t": 0.0}},
            {"name": "cz", "locus": ["QB1", "QB2"], "args": {}},
            {"name": "prx", "locus": ["QB2"], "args": {"angle_t": 0.5, "phase_t": 0.0}},
            {"name": "measure", "locus": ["QB1"], "args": {"key": "QB1"}},
            {"name": "measure", "locus": ["QB2"], "args": {"key": "QB2"}}
        ]
    })";

    IQM_QDMI_Device_Job job;
    IQM_QDMI_device_session_create_device_job(session, &job);

    QDMI_Program_Format format = QDMI_PROGRAM_FORMAT_IQMJSON;
    size_t shots = 100;
    IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(format), &format);
    IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, strlen(circuit) + 1, circuit);
    IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots);

    IQM_QDMI_device_job_submit(job);
    IQM_QDMI_device_job_wait(job, 0);

    QDMI_Job_Status status;
    IQM_QDMI_device_job_check(job, &status);
    if (status == QDMI_JOB_STATUS_DONE) {
        std::cout << "Job completed successfully!\n";
    }

    IQM_QDMI_device_session_free(session);
    IQM_QDMI_device_finalize();
    return 0;
}
```

For complete examples, see the [Usage Guide](docs/usage.md).

## How It Works

1. **Session Management**: Initialize a session with an IQM server endpoint and authentication
2. **Device Discovery**: Automatically fetches quantum devices and calibration data
3. **Circuit Submission**: Submit quantum circuits in QIR or IQM JSON format
4. **Result Retrieval**: Poll for job completion and retrieve measurement results
5. **Calibration Control**: Optionally trigger calibrations and update to new calibration sets

The library interfaces with the [IQM Server API](https://resonance.meetiqm.com/docs) to provide complete device
management through the standardized QDMI interface.

## Testing

```console
# Build with tests
$ cmake -S . -B build -DBUILD_IQM_QDMI_TESTS=ON
$ cmake --build build
```

```console
# Run all tests
$ ctest --test-dir build --output-on-failure

# Run only unit tests (no hardware required)
$ ctest --test-dir build/test/unit --output-on-failure

# Run integration tests (requires IQM access)
$ export IQM_BASE_URL="https://desired-iqm-server.com"
$ export RESONANCE_API_KEY="your-api-key"
$ ctest --test-dir build/test/integration --output-on-failure
```

## Contributing

We welcome contributions! Whether you're fixing bugs, improving documentation, or proposing new features, your help is
appreciated.

Please see our [Contributing Guide](docs/contributing.md) for:

- How to report bugs and request features
- Development workflow and coding standards
- Testing and documentation requirements
- Pull request process

## Getting Help

- **Documentation**: Check the [docs/](docs/) directory
- **Discussions**: Ask questions in [GitHub Discussions](https://github.com/iqm-finland/QDMI-on-IQM/discussions)
- **Bug Reports**: Open an issue in [GitHub Issues](https://github.com/iqm-finland/QDMI-on-IQM/issues)
- **Security**: Report vulnerabilities via our [Security Policy](.github/SECURITY.md)

## License

This project is licensed under the Apache License 2.0 with LLVM exceptions. See the [LICENSE.md](LICENSE.md) file for details.
