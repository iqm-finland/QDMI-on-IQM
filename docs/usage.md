# Usage Guide

This guide demonstrates how to use the IQM QDMI Device library to communicate with IQM's quantum computing hardware.

## Configuring the QDMI Device

The QDMI device connects to the unified IQM Server API to communicate with IQM's quantum computing hardware.

### Authentication Methods

The QDMI device supports multiple authentication methods:

1. **Environment Variables** (recommended for ease of use):
   - `IQM_TOKEN`: Bearer token for authentication
   - `IQM_TOKENS_FILE`: Path to a file containing authentication tokens

2. **Explicit Parameters**: Authentication credentials can be set programmatically via session parameters.

**Important**: Authentication credentials are resolved in the following order:

- If explicit parameters are set via `QDMI_DEVICE_SESSION_PARAMETER_TOKEN` or `QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE`, they take precedence and the environment variables are ignored.
- If no explicit parameters are set, the device will automatically use the `IQM_TOKEN` or `IQM_TOKENS_FILE` environment variables if they are defined.
- If both environment variables and explicit parameters are set simultaneously, the explicit parameters will take precedence.

### Session Configuration

To initiate a session with a particular endpoint and authentication method, the following code snippet can be used:

```cpp
IQM_QDMI_Device_Session
FoMaC::get_iqm_session(const std::string &base_url,
                       const std::optional<std::string> &quantum_computer_id,
                       const std::optional<std::string> &quantum_computer_alias,
                       const std::optional<std::string> &token,
                       const std::optional<std::string> &tokens_file) {
  IQM_QDMI_Device_Session session = nullptr;
  auto ret = IQM_QDMI_device_session_alloc(&session);

  // Set the base URL for the IQM server
  ret = IQM_QDMI_device_session_set_parameter(
      session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, base_url.size() + 1,
      base_url.c_str());

  // Optionally specify which quantum computer to use by ID
  if (quantum_computer_id.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1,
        quantum_computer_id->size() + 1, quantum_computer_id->c_str());
  }

  // Optionally specify which quantum computer to use by alias
  if (quantum_computer_alias.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2,
        quantum_computer_alias->size() + 1, quantum_computer_alias->c_str());
  }

  // Set authentication token if provided
  if (token.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_TOKEN, token->size() + 1,
        token->c_str());
  }

  // Set tokens file path if provided
  if (tokens_file.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE,
        tokens_file->size() + 1, tokens_file->c_str());
  }

  // Initialize the session
  IQM_QDMI_device_session_init(session);

  return session;
}
```

The \ref IQM_QDMI_device_session_alloc function allocates a new session object, and the \ref IQM_QDMI_device_session_set_parameter function is used to set various parameters for the session:

- **Base URL** (`QDMI_DEVICE_SESSION_PARAMETER_BASEURL`): The URL of the IQM server.
- **Quantum Computer ID** (`QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1`): Optional ID of the specific quantum computer to use.
- **Quantum Computer Alias** (`QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2`): Optional alias of the specific quantum computer to use.
- **Authentication Token** (`QDMI_DEVICE_SESSION_PARAMETER_TOKEN`): Bearer token for authentication. If not set, falls back to the `IQM_TOKEN` environment variable.
- **Tokens File** (`QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE`): Path to a file containing authentication tokens. If not set, falls back to the `IQM_TOKENS_FILE` environment variable.

**Note on Authentication**: If you set either authentication parameter explicitly, the corresponding environment variable will be ignored. This allows you to override environment-based authentication when needed.

If neither quantum computer ID nor alias is specified, the first available quantum computer from the server will be used.

The session is initialized with \ref IQM_QDMI_device_session_init, which:

1. Fetches the list of available quantum computers from the server
2. Selects the appropriate quantum computer (by ID, alias, or first available)
3. Retrieves the static quantum architecture (qubits and connectivity)
4. Fetches the dynamic quantum architecture with calibrated gates for the default calibration set
5. Retrieves calibration metrics (T1/T2 times, gate fidelities) if available
6. Checks whether calibration jobs are supported by the server

At any time, `ret` is the `QDMI_STATUS` return value of the last function call, which can be checked for error codes.

### Session Initialization Details

When `IQM_QDMI_device_session_init` is called, the following steps occur:

1. **Authentication Setup**: The token manager is initialized with the provided authentication credentials.

2. **Quantum Computer Selection**:
   - The system fetches the list of available quantum computers from the server.
   - If a quantum computer ID was specified, it searches for that ID and retrieves the corresponding alias.
   - If a quantum computer alias was specified, it searches for that alias and retrieves the corresponding ID.
   - If neither was specified, the first available quantum computer is selected.

3. **Static Architecture Retrieval**:
   - The static quantum architecture is fetched, containing the qubits and their connectivity.
   - This information is stored in memory for efficient querying.

4. **Dynamic Architecture Retrieval**:
   - The dynamic quantum architecture is fetched for the "default" calibration set.
   - This includes the list of calibrated gates and their implementations.
   - The calibration set ID is stored for use in job submissions.

5. **Quality Metrics Retrieval**:
   - If available, calibration metrics are fetched from the server.
   - This includes T1 and T2 coherence times for qubits.
   - Gate fidelities for single-qubit (prx, measure) and two-qubit (cz) operations.

6. **Calibration Job Support Check**:
   - The system checks if the server supports calibration jobs by querying the COCOS health endpoint.
   - This determines whether `QDMI_PROGRAM_FORMAT_CALIBRATION` jobs can be submitted.

After initialization, the session is ready to submit jobs and query device information.

## Understanding Quantum Architecture and Calibration Sets

The IQM Server API distinguishes between two types of quantum architecture:

### Static Quantum Architecture

The **static quantum architecture** defines the physical layout of the quantum computer:

- **Qubits**: The set of available qubits (e.g., "QB1", "QB2", "QB3", etc.)
- **Connectivity**: The coupling map showing which qubits are connected and can interact

This information is fixed for a given quantum computer and fetched once during session initialization.

### Dynamic Quantum Architecture

The **dynamic quantum architecture** defines the calibrated operations available on the quantum computer:

- **Calibrated Gates**: The set of gates that are currently calibrated and ready to use
- **Gate Implementations**: The specific implementations of each gate (e.g., "phased_rx" for prx)
- **Calibration Set ID**: A unique identifier for the current calibration data

The dynamic architecture is tied to a specific **calibration set**. Each time the quantum computer is calibrated, a new calibration set is created with updated gate implementations and quality metrics.

### Calibration Sets

A **calibration set** represents a snapshot of the quantum computer's calibration data at a specific point in time. It includes:

- The set of calibrated gates and their implementations
- Quality metrics for qubits (T1, T2 coherence times)
- Quality metrics for operations (gate fidelities)

When you initialize a session, the system uses the "default" calibration set (typically the most recent calibration). You can trigger new calibrations using calibration jobs, which create new calibration sets and automatically update the session to use them.

## Querying Device Information

The QDMI device allows you to query various information about the quantum computing hardware, such as the available qubits, operations, and their properties.
All device information is fetched during session initialization and kept in memory for efficient querying.

The following properties about the device can be queried via the \ref IQM_QDMI_device_session_query_device_property function:

- `QDMI_DEVICE_PROPERTY_NAME`: The name/alias of the quantum computer.
- `QDMI_DEVICE_PROPERTY_VERSION`: The version of the IQM QDMI device implementation.
- `QDMI_DEVICE_PROPERTY_LIBRARYVERSION`: The version of the QDMI library.
- `QDMI_DEVICE_PROPERTY_STATUS`: The current status of the device (e.g., idle, busy).
- `QDMI_DEVICE_PROPERTY_QUBITSNUM`: The number of qubits available on the device.
- `QDMI_DEVICE_PROPERTY_SITES`: The list of available qubit sites on the device.
- `QDMI_DEVICE_PROPERTY_OPERATIONS`: The list of available calibrated operations on the device.
- `QDMI_DEVICE_PROPERTY_COUPLINGMAP`: The coupling map between qubits on the device.

The following properties about every site (qubit) can be queried via the \ref IQM_QDMI_device_session_query_site_property function:

- `QDMI_SITE_PROPERTY_NAME`: The name of the qubit (e.g., "QB1", "QB2").
- `QDMI_SITE_PROPERTY_INDEX`: The index of the qubit.
- `QDMI_SITE_PROPERTY_T1`: The T1 coherence time of the qubit in microseconds (if available).
- `QDMI_SITE_PROPERTY_T2`: The T2 coherence time of the qubit in microseconds (if available).

The following properties about every operation can be queried via the \ref IQM_QDMI_device_session_query_operation_property function:

- `QDMI_OPERATION_PROPERTY_NAME`: The name of the operation (e.g., "prx", "cz", "measure").
- `QDMI_OPERATION_PROPERTY_QUBITSNUM`: The number of qubits the operation acts on.
- `QDMI_OPERATION_PROPERTY_PARAMETERSNUM`: The number of parameters the operation has.
- `QDMI_OPERATION_PROPERTY_FIDELITY`: The fidelity of the operation (if available, may be qubit-specific).

**Note:** The available operations are determined by the current calibration set. Quality metrics (T1, T2, fidelities) are fetched from the server's calibration set quality metrics endpoint if available. The QDMI device does not support querying operation durations, as this information is not provided by the IQM Server API.

## Submitting jobs

The QDMI device allows you to submit jobs to the quantum computing hardware.
The following code snippet demonstrates how to submit a job with various parameters, including the

- **program**: The quantum program to be executed,
- **program format**: The format used for the program,
- **number of shots**: The number of shots to execute for a quantum circuit job,
- **heralding mode**: Controls heralding behavior (valid values: "none", "zeros"), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM1`,
- **move validation mode**: Validation level for moves (valid values: "strict", "allow_prx", "none"), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM2`,
- **move gate frame tracking mode**: Frame tracking behavior (valid values: "full", "no_detuning_correction", "none"), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM3`,
- **dynamical decoupling mode**: Enable/disable dynamical decoupling (valid values: "disabled", "enabled"), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM4`,
- **qubit mapping**: Mapping of logical qubit names to physical qubit names (important for QIR programs), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM5`,
- **maximum circuit duration over T2**: The maximum duration of a circuit over T2 time, set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 1`,
- **number of active reset cycles**: The number of active reset cycles to perform, set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2`, and
- **dynamical decoupling strategy**: Strategy configuration as JSON string according to the [IQM SDK data model](https://github.com/iqm-finland/sdk/blob/1a563651751bb0779026fcc7f45d8ca676c365c3/iqm_client/src/iqm/iqm_client/models.py#L791), set via `QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 3`.

```cpp
auto FoMaC::submit_job(
    const std::string &program, const QDMI_Program_Format format,
    const size_t num_shots, const std::string &heralding_mode,
    const std::string &move_validation_mode,
    const std::string &move_gate_frame_tracking_mode,
    const std::string &dd_mode,
    const std::optional<std::map<std::string, std::string>> &qubit_mapping,
    const std::optional<double> &max_circuit_duration_over_t2,
    const std::optional<size_t> &num_active_reset_cycles,
    const std::optional<std::string> &dd_strategy) const
    -> IQM_QDMI_Device_Job {
  IQM_QDMI_Device_Job job = nullptr;
  int ret = IQM_QDMI_device_session_create_device_job(session_, &job);
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(QDMI_Program_Format),
      &format);
  ret =
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                                        program.size() + 1, program.c_str());
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(size_t), &num_shots);
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM1, heralding_mode.size() + 1,
      heralding_mode.c_str());
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM2, move_validation_mode.size() + 1,
      move_validation_mode.c_str());
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM3,
      move_gate_frame_tracking_mode.size() + 1,
      move_gate_frame_tracking_mode.c_str());
  ret =
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM4,
                                        dd_mode.size() + 1, dd_mode.c_str());
  if (qubit_mapping.has_value()) {
    std::string mapping_str;
    for (const auto &pair : *qubit_mapping) {
      mapping_str += pair.first + ":" + pair.second + ",";
    }
    if (!mapping_str.empty()) {
      mapping_str.pop_back(); // Remove the trailing comma
    }
    ret = IQM_QDMI_device_job_set_parameter(
        job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM5, mapping_str.size() + 1,
        mapping_str.c_str());
  }
  if (max_circuit_duration_over_t2.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 1),
        sizeof(double), &max_circuit_duration_over_t2.value());
  }
  if (num_active_reset_cycles.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2),
        sizeof(size_t), &num_active_reset_cycles.value());
  }
  if (dd_strategy.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 3),
        dd_strategy->size() + 1, dd_strategy->c_str());
  }
  ret = IQM_QDMI_device_job_submit(job);
  return job;
}
```

The \ref IQM_QDMI_device_session_create_device_job function creates a new job object, and the \ref IQM_QDMI_device_job_set_parameter function is used to set various parameters for the job.
The job is submitted with \ref IQM_QDMI_device_job_submit, which sends the job to the quantum computing hardware for execution.
As before, `ret` is the `QDMI_STATUS` return value of the last function call, which can be checked for error codes.

**Important:** When submitting circuit jobs (QIR or IQM JSON), the implementation automatically includes the current calibration set ID in the job submission. This ensures that the job uses the same calibrated gates that were available when the session was initialized or last updated.

The QDMI device currently supports the following program formats:

- **QIR Base Profile Strings** (`QDMI_PROGRAM_FORMAT_QIRBASESTRING`): QIR base profile programs as strings.
- **IQM JSON** (`QDMI_PROGRAM_FORMAT_IQMJSON`): IQM's native JSON circuit format.
- **Calibration Configurations** (`QDMI_PROGRAM_FORMAT_CALIBRATION`): Calibration job configurations (only if server supports calibration jobs).

For QIR and JSON formats, the program should be provided as a string via the `QDMI_DEVICE_JOB_PARAMETER_PROGRAM` parameter.

## Retrieving Job Results

After a job completes execution, you can retrieve the measurement results in different formats. The IQM QDMI device supports retrieving results as histogram counts or as individual shot measurements.

### Result Formats

The following result formats are supported:

- **`QDMI_JOB_RESULT_HIST_KEYS`**: Returns the bitstring keys from the measurement histogram as a comma-separated string.
- **`QDMI_JOB_RESULT_HIST_VALUES`**: Returns the corresponding count values as an array of `size_t`.
- **`QDMI_JOB_RESULT_SHOTS`**: Returns individual shot measurements as a comma-separated string of bitstrings.

### Retrieving Histogram Results

Histogram results provide aggregated measurement counts for each unique outcome. This is the most common format for analyzing quantum circuit results.

```cpp
// Wait for job completion
IQM_QDMI_device_job_wait(job, 0);

// Get histogram keys (bitstrings)
size_t keys_size = 0;
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                0, nullptr, &keys_size);
std::vector<char> keys_buffer(keys_size);
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                keys_size, keys_buffer.data(), nullptr);
std::string keys(keys_buffer.data());
// keys contains: "00,01,10,11" (example)

// Get histogram values (counts)
size_t values_size = 0;
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                0, nullptr, &values_size);
std::vector<size_t> values(values_size / sizeof(size_t));
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                values_size, values.data(), nullptr);
// values contains: {25, 15, 18, 6} (example counts for each key)
```

The histogram keys are returned as a comma-separated string, and the values are returned as an array of counts. The keys and values are in the same order, so after parsing the keys string by splitting on commas, the i-th parsed key corresponds to `values[i]`.

Example of parsing the keys string:

```cpp
// Parse keys string into individual bitstrings
std::vector<std::string> key_list;
std::stringstream ss(keys);
std::string token;
while (std::getline(ss, token, ',')) {
  key_list.push_back(token);
}

// Now key_list[i] corresponds to values[i]
for (size_t i = 0; i < key_list.size(); ++i) {
  std::cout << "Outcome " << key_list[i] << ": " << values[i] << " times\n";
}
```

### Retrieving Individual Shot Measurements

Individual shot measurements provide the raw measurement outcome for each execution of the circuit. This is useful for analyzing shot-to-shot correlations or performing custom post-processing.

```cpp
// Wait for job completion
IQM_QDMI_device_job_wait(job, 0);

// Get individual shots
size_t shots_size = 0;
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                0, nullptr, &shots_size);
std::vector<char> shots_buffer(shots_size);
IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                shots_size, shots_buffer.data(), nullptr);
std::string shots(shots_buffer.data());
// shots contains: "00,10,01,11,00,10,..." (one bitstring per shot)
```

Each bitstring in the result represents the measurement outcome for one shot. The bitstrings are ordered chronologically (shot 1, shot 2, shot 3, etc.).

### Unsupported Result Formats

The following QDMI standard result formats (see `QDMI_JOB_RESULT_T`) are **not supported** by the IQM QDMI device because IQM quantum computers return measurement data, not state vectors or probability distributions:

- `QDMI_JOB_RESULT_STATEVECTOR_DENSE`
- `QDMI_JOB_RESULT_STATEVECTOR_SPARSE_KEYS` / `QDMI_JOB_RESULT_STATEVECTOR_SPARSE_VALUES`
- `QDMI_JOB_RESULT_PROBABILITIES_DENSE`
- `QDMI_JOB_RESULT_PROBABILITIES_SPARSE_KEYS` / `QDMI_JOB_RESULT_PROBABILITIES_SPARSE_VALUES`

Attempting to retrieve these formats will return `QDMI_ERROR_NOTSUPPORTED`.

## Triggering Calibration Jobs

Calibrations can be triggered using the `QDMI_PROGRAM_FORMAT_CALIBRATION` program format.
This feature is only available if the IQM server supports calibration jobs (checked during session initialization).
The payload should contain the calibration configuration as a JSON string according to the IQM Server API specification.

The results can be retrieved via the `QDMI_JOB_RESULT_CUSTOM1` job result parameter on a calibration job, which returns the new calibration set ID.

**Important:** When querying the result of a calibration job, the system will:

1. Extract the new calibration set ID from the job result
2. Automatically update the session to use the new calibration set
3. Fetch the updated dynamic quantum architecture with the new calibrated gates
4. Retrieve the updated calibration metrics (T1/T2 times, gate fidelities)

This automatic update invalidates all previously obtained `IQM_QDMI_Operation` pointers and the quality metrics associated with the qubits and operations. You should re-query device information after a calibration job completes.

Here's an example of submitting a calibration job:

```cpp
auto *job = fomac.submit_job(TEST_CALIBRATION_CONFIG,
                               QDMI_PROGRAM_FORMAT_CALIBRATION);
IQM_QDMI_device_job_wait(job, 0);

// Get the new calibration set ID
const auto calibration_set_id = FoMaC::get_calibration_set_id(job);
// The session is now updated with the new calibration data
```

**Note:** Calibration jobs use different API endpoints than regular circuit jobs:

- Submit: `/cocos/api/v4/calibration/runs` (calibration job endpoint)
- Status: `/cocos/api/v4/calibration/runs/<job_id>/status` (calibration job status endpoint)
- Abort: `/cocos/api/v4/calibration/runs/<job_id>/abort` (calibration job abort endpoint)

## Retrieving error logs

If a submitted job fails, the QDMI device will automatically log detailed error information to help diagnose the problem.
All errors are logged as ERROR level messages, and any informational messages are logged as DEBUG level messages.

When you check a job's status using `IQM_QDMI_device_job_check()` and the job has failed, all errors and messages will be automatically logged:

```cpp
auto *job = fomac.submit_job(TEST_PROGRAM, QDMI_PROGRAM_FORMAT_QIRBASESTRING);
IQM_QDMI_device_job_wait(job, 0);

QDMI_Job_Status status;
IQM_QDMI_device_job_check(job, &status);
// All errors and messages have already been logged automatically
// Check your log output for details about the failure
```

## Logging

The project provides a simple logging mechanism to help you debug your application.
You can control the logging level by setting the `IQM_CPP_API_LOG_LEVEL` environment variable.
The following logging levels are available:

- `NONE`: No logging.
- `ERROR`: Log only errors.
- `INFO`: Log errors and info messages.
- `DEBUG`: Log errors, info, and debug messages.

By default, the logging level is set to `ERROR`.

The following macros are available for logging:

- `LOG_ERROR(msg)`: Log an error message.
- `LOG_INFO(msg)`: Log an info message.
- `LOG_DEBUG(msg)`: Log a debug message.

By default, the logger prints to `std::cerr`. You can redirect the output to a file or any other stream using the `set_output` method of the `Logger` class.

Here's an example of how to redirect the log output to a file and log info messages:

```cpp
#include "logging.hpp"

#include <ofstream>

int main() {
  std::ofstream log_file("iqm_qdmi.log");
  iqm::Logger::get_instance().set_output(log_file);
  iqm::Logger::get_instance().set_level(iqm::Logger::Level::INFO);

  LOG_INFO("This message will be written to iqm_qdmi.log");

  return 0;
}
```
