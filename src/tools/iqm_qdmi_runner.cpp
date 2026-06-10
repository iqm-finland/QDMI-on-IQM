/*
 * Copyright (c) 2025 - 2026 IQM Finland Oy
 * All rights reserved.
 *
 * Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iqm_qdmi/constants.h>
#include <iqm_qdmi/device.h>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] auto Split_histogram_keys(const std::string_view csv_keys)
    -> std::vector<std::string> {
  std::vector<std::string> keys;
  for (const auto part : csv_keys | std::views::split(',')) {
    const auto begin = std::ranges::begin(part);
    const auto end = std::ranges::end(part);
    keys.emplace_back(begin, end);
  }
  return keys;
}

} // namespace

/**
 * @brief Standalone CLI tool to submit quantum circuits to an IQM QPU via QDMI.
 *
 * Usage: iqm-qdmi-runner <circuit_file.json>
 */
int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <circuit_file.json>\n";
    return EXIT_FAILURE;
  }

  const std::string filename = argv[1];
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file '" << filename << "'\n";
    return EXIT_FAILURE;
  }

  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  file.close();

  if (content.empty()) {
    std::cerr << "Error: Circuit file is empty.\n";
    return EXIT_FAILURE;
  }

  if (!std::string_view(filename).ends_with(".json")) {
    std::cerr << "Error: Unsupported circuit format for '" << filename
              << "'. Only .json files are supported.\n";
    return EXIT_FAILURE;
  }

  IQM_QDMI_Device_Session session = nullptr;
  if (IQM_QDMI_device_session_alloc(&session) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to allocate QDMI session.\n";
    return EXIT_FAILURE;
  }

  if (IQM_QDMI_device_session_init(session) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to initialize QDMI session. Check your "
                 "IQM_BASE_URL and authentication environment variables."
              << "\n";
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  IQM_QDMI_Device_Job job = nullptr;
  if (IQM_QDMI_device_session_create_device_job(session, &job) !=
      QDMI_SUCCESS) {
    std::cerr << "Error: Failed to create QDMI job.\n";
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Set program content
  if (IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                                        content.size() + 1,
                                        content.c_str()) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to set job program.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Set program format.
  const QDMI_Program_Format format = QDMI_PROGRAM_FORMAT_IQMJSON;
  if (IQM_QDMI_device_job_set_parameter(
          job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(format),
          &format) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to set program format.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Submit the job
  if (IQM_QDMI_device_job_submit(job) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to submit job to the QPU.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Reuse the device implementation's polling/backoff behavior.
  if (IQM_QDMI_device_job_wait(job, 0) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed while waiting for job completion.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  QDMI_Job_Status status = QDMI_JOB_STATUS_CREATED;
  if (IQM_QDMI_device_job_check(job, &status) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to read final job status.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  if (status != QDMI_JOB_STATUS_DONE) {
    std::cerr << "Error: Job did not complete successfully (status code: "
              << static_cast<int>(status) << ").\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Retrieve results
  size_t key_size = 0;
  if (IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                      nullptr, &key_size) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to query result key size.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }
  if (key_size == 0) {
    std::cerr << "Error: Job completed but returned no result keys.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  std::string keys_str(key_size, '\0');
  if (IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, key_size,
                                      keys_str.data(),
                                      nullptr) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to fetch result keys.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }
  if (!keys_str.empty() && keys_str.back() == '\0') {
    keys_str.pop_back();
  }

  size_t val_size = 0;
  if (IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES, 0,
                                      nullptr, &val_size) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to query result value size.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }
  std::vector<size_t> values(val_size / sizeof(size_t));
  if (IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                      val_size, values.data(),
                                      nullptr) != QDMI_SUCCESS) {
    std::cerr << "Error: Failed to fetch result values.\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  // Parse keys and print as JSON.
  const auto key_list = Split_histogram_keys(keys_str);

  if (key_list.size() != values.size()) {
    std::cerr << "Error: Mismatch between key count (" << key_list.size()
              << ") and value count (" << values.size() << ").\n";
    IQM_QDMI_device_job_free(job);
    IQM_QDMI_device_session_free(session);
    return EXIT_FAILURE;
  }

  std::cout << "{";
  for (std::size_t i = 0; i < key_list.size(); ++i) {
    std::cout << "\"" << key_list[i] << "\": " << values[i];
    if (i < key_list.size() - 1) {
      std::cout << ", ";
    }
  }
  std::cout << "}\n";

  IQM_QDMI_device_job_free(job);
  IQM_QDMI_device_session_free(session);
  return EXIT_SUCCESS;
}
