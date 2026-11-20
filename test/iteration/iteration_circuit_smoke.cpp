/*
 * Copyright (c) 2025 - 2026 IQM Finland Oy
 * All rights reserved.
 *
 * Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "iqm_qdmi/device.h"

#include "iteration_management_smoke.hpp"
#include "qdmi_client.hpp"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void Require_success(const int status, const std::string &operation) {
  if (status != QDMI_SUCCESS) {
    throw std::runtime_error(operation + " returned " + std::to_string(status));
  }
}

bool Run_circuit(const char *qc, const bool qc_is_id, const std::string &id,
                 const bool flip, const bool ended) {
  IQM_QDMI_Device_Session session = nullptr;
  Require_success(IQM_QDMI_device_session_alloc(&session), "Allocate session");
  const std::unique_ptr<IQM_QDMI_Device_Session_impl_d,
                        decltype(&IQM_QDMI_device_session_free)>
      session_guard{session, IQM_QDMI_device_session_free};
  const auto selector = qc_is_id ? QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1
                                 : QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2;
  Require_success(IQM_QDMI_device_session_set_parameter(
                      session, selector, std::strlen(qc) + 1, qc),
                  "Set quantum computer");
  Require_success(IQM_QDMI_device_session_set_parameter(
                      session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM5,
                      id.size() + 1, id.c_str()),
                  "Set iteration ID");
  constexpr std::string_view timeout = "timeout_ms:5000";
  Require_success(IQM_QDMI_device_session_set_parameter(
                      session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                      timeout.size() + 1, timeout.data()),
                  "Set request timeout");
  Require_success(IQM_QDMI_device_session_init(session), "Initialize session");

  IQM_QDMI_Device_Job job = nullptr;
  Require_success(IQM_QDMI_device_session_create_device_job(session, &job),
                  "Create job");
  const std::unique_ptr<IQM_QDMI_Device_Job_impl_d,
                        decltype(&IQM_QDMI_device_job_free)>
      job_guard{job, IQM_QDMI_device_job_free};
  std::string circuit = R"({"name":"iteration-smoke","instructions":[)";
  if (flip) {
    circuit +=
        R"({"name":"prx","locus":["QB1"],"args":{"angle":0.5,"phase":0}},)";
  }
  circuit += R"({"name":"measure","locus":["QB1"],"args":{"key":"m"}}]})";
  const size_t size = circuit.size() + 1;
  const void *program = circuit.c_str();
  Require_success(IQM_QDMI_device_job_set_programs(
                      job, QDMI_PROGRAM_FORMAT_IQMJSON, 1, &size, &program),
                  "Set circuit");
  constexpr size_t shots = 16;
  Require_success(
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM,
                                        sizeof(shots), &shots),
      "Set shots");
  if (ended) {
    const std::ostringstream diagnostics;
    auto *previous = std::cerr.rdbuf(diagnostics.rdbuf());
    const int submitted = IQM_QDMI_device_job_submit(job);
    std::cerr.rdbuf(previous);
    std::cerr << diagnostics.str();
    if (submitted != QDMI_ERROR_FATAL ||
        diagnostics.view().find(R"("http_status":409)") == std::string::npos) {
      throw std::runtime_error("Expected an HTTP 409 for the ended iteration");
    }
    return false;
  }
  Require_success(IQM_QDMI_device_job_submit(job), "Submit circuit");
  Require_success(IQM_QDMI_device_job_wait(job, 20), "Wait for circuit");
  const auto counts = QDMIClient::get_histogram(job);
  const auto zeros = counts.contains("0") ? counts.at("0") : 0;
  const auto ones = counts.contains("1") ? counts.at("1") : 0;
  if (zeros + ones != shots) {
    throw std::runtime_error("Circuit returned an unexpected histogram");
  }
  return ones > zeros;
}

void Check_circuits(const char *qc, const bool qc_is_id, const std::string &id,
                    const bool ended) {
  if (ended) {
    Run_circuit(qc, qc_is_id, id, false, true);
    std::cout << "Ended iteration refused circuit submission\n";
    return;
  }
  const bool measured_one = Run_circuit(qc, qc_is_id, id, false, false);
  Run_circuit(qc, qc_is_id, id, !measured_one, false);
  std::cout << "Retrieved both circuit results from iteration " << id << '\n';
}
} // namespace

// Keep the portable main signature used by every supported compiler.
// NOLINTNEXTLINE(misc-const-correctness)
int main(const int argc, char **argv) {
  using iqm::test_support::Iteration_management_smoke;
  if (argc == 1) {
    return Iteration_management_smoke(false);
  }
  if (argc == 2 && std::string_view{argv[1]} == "--cleanup-only") {
    return Iteration_management_smoke(true);
  }
  if (argc != 2 || std::string_view{argv[1]} != "--circuits") {
    return 1;
  }
  if (IQM_QDMI_device_initialize() != QDMI_SUCCESS) {
    return 1;
  }
  const int result = Iteration_management_smoke(false, Check_circuits);
  return IQM_QDMI_device_finalize() == QDMI_SUCCESS ? result : 1;
}
