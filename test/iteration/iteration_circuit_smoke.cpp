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
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace iqm::test_support {
namespace {
bool Run_circuit(IQM_QDMI_Device_Session session, const std::string &id,
                 const bool flip, const bool ended) {
  const auto job = Create_job(session);
  Require_success(IQM_QDMI_device_job_set_parameter(
                      job.get(), QDMI_DEVICE_JOB_PARAMETER_CUSTOM2,
                      id.size() + 1, id.c_str()),
                  "Set iteration ID");
  std::string circuit = R"({"name":"iteration-smoke","instructions":[)";
  if (flip) {
    circuit +=
        R"({"name":"prx","locus":["QB1"],"args":{"angle":0.5,"phase":0.0}},)";
  }
  circuit += R"({"name":"measure","locus":["QB1"],"args":{"key":"m"}}]})";
  const size_t size = circuit.size() + 1;
  const void *program = circuit.c_str();
  Require_success(IQM_QDMI_device_job_set_programs(job.get(),
                                                   QDMI_PROGRAM_FORMAT_IQMJSON,
                                                   1, &size, &program),
                  "Set circuit");
  constexpr size_t shots = 16;
  Require_success(
      IQM_QDMI_device_job_set_parameter(
          job.get(), QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
      "Set shots");
  if (ended) {
    const std::ostringstream diagnostics;
    auto *previous = std::cerr.rdbuf(diagnostics.rdbuf());
    const int submitted = IQM_QDMI_device_job_submit(job.get());
    std::cerr.rdbuf(previous);
    std::cerr << diagnostics.str();
    if (submitted != QDMI_ERROR_FATAL ||
        diagnostics.view().find(R"("http_status":409)") == std::string::npos) {
      throw std::runtime_error("Expected an HTTP 409 for the ended iteration");
    }
    return false;
  }
  Require_success(IQM_QDMI_device_job_submit(job.get()), "Submit circuit");
  Require_success(IQM_QDMI_device_job_wait(job.get(), 90), "Wait for circuit");
  const auto counts = QDMIClient::get_histogram(job.get());
  const auto zeros = counts.contains("0") ? counts.at("0") : 0;
  const auto ones = counts.contains("1") ? counts.at("1") : 0;
  if (zeros + ones != shots) {
    throw std::runtime_error("Circuit returned an unexpected histogram");
  }
  return ones > zeros;
}

} // namespace

void Check_circuits(IQM_QDMI_Device_Session session, const std::string &id,
                    const bool ended) {
  if (ended) {
    Run_circuit(session, id, false, true);
    std::cout << "Ended iteration refused circuit submission\n";
    return;
  }
  const bool measured_one = Run_circuit(session, id, false, false);
  Run_circuit(session, id, !measured_one, false);
  std::cout << "Retrieved both circuit results from iteration " << id << '\n';
}
} // namespace iqm::test_support
