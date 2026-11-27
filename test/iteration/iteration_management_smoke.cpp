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

#include "iteration_management_smoke.hpp"

#include "iqm_qdmi/device.h"

#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace iqm::test_support {
void Require_success(const int status, const std::string &operation) {
  if (status != QDMI_SUCCESS) {
    throw std::runtime_error(operation + " returned " + std::to_string(status));
  }
}

Job Create_job(IQM_QDMI_Device_Session session) {
  IQM_QDMI_Device_Job raw = nullptr;
  Require_success(IQM_QDMI_device_session_create_device_job(session, &raw),
                  "Create job");
  return Job{raw, IQM_QDMI_device_job_free};
}

namespace {
bool Canonical_id(const std::string_view id) {
  if (id.size() != 36) {
    return false;
  }
  for (size_t i = 0; i < id.size(); ++i) {
    const auto c = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
    } else if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
      return false;
    }
  }
  return true;
}
std::vector<std::string> Read_ids(const std::string &path) {
  const auto status = std::filesystem::status(path);
  if (std::filesystem::exists(status) &&
      !std::filesystem::is_regular_file(status)) {
    throw std::runtime_error("Cannot read scratch IDs");
  }
  std::ifstream input{path, std::ios::binary};
  if (!input && std::filesystem::exists(path)) {
    throw std::runtime_error("Cannot read scratch IDs");
  }
  std::vector<std::string> ids;
  for (std::string id; std::getline(input, id);) {
    if ((id.size() == 37 || id.size() == 1) && id.back() == '\r') {
      id.pop_back();
    }
    if (!id.empty()) {
      ids.push_back(id);
    }
  }
  if (input.bad()) {
    throw std::runtime_error("Cannot read scratch IDs");
  }
  return ids;
}
void Remove_id(const std::string &path, const std::string &id) {
  const auto ids = Read_ids(path);
  const std::filesystem::path temporary{path + ".tmp"};
  std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
  if (!file) {
    throw std::runtime_error("Cannot write scratch replacement");
  }
  try {
    for (const auto &entry : ids) {
      if (entry != id) {
        file << entry << '\n';
      }
    }
    file.flush();
    if (!file) {
      throw std::runtime_error("Cannot flush scratch replacement");
    }
    file.close();
    if (!file) {
      throw std::runtime_error("Cannot close scratch replacement");
    }
    std::filesystem::rename(temporary, path);
  } catch (...) {
    file.close();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

bool Terminal(const QDMI_Job_Status state) {
  return state == QDMI_JOB_STATUS_DONE || state == QDMI_JOB_STATUS_FAILED ||
         state == QDMI_JOB_STATUS_CANCELED;
}

QDMI_Job_Status Check(IQM_QDMI_Device_Job job) {
  QDMI_Job_Status status{};
  Require_success(IQM_QDMI_device_job_check(job, &status),
                  "Inspect reservation");
  return status;
}

void Set_reservation(IQM_QDMI_Device_Job job, const std::string &command) {
  const size_t size = command.size() + 1;
  const void *program = command.c_str();
  Require_success(IQM_QDMI_device_job_set_programs(
                      job, QDMI_PROGRAM_FORMAT_CUSTOM2, 1, &size, &program),
                  "Set reservation program");
}

void End(IQM_QDMI_Device_Session session, const std::string &id) {
  const auto end = Create_job(session);
  Set_reservation(end.get(), R"({"end":")" + id + R"("})");
  Require_success(IQM_QDMI_device_job_submit(end.get()),
                  "End reservation " + id);
  Require_success(IQM_QDMI_device_job_wait(end.get(), 30),
                  "Wait for termination of " + id);
  if (!Terminal(Check(end.get()))) {
    throw std::runtime_error("Termination unconfirmed for " + id);
  }
}

void Reconcile(IQM_QDMI_Device_Session session, const std::string &id) {
  if (!Canonical_id(id)) {
    throw std::runtime_error("Invalid scratch ID");
  }
  IQM_QDMI_Device_Job raw = nullptr;
  Require_success(IQM_QDMI_device_session_retrieve_device_job_by_id(
                      session, id.c_str(), &raw),
                  "Retrieve reservation " + id);
  const Job observer{raw, IQM_QDMI_device_job_free};
  QDMI_Program_Format format{};
  Require_success(IQM_QDMI_device_job_query_property(
                      raw, QDMI_DEVICE_JOB_PROPERTY_PROGRAMFORMAT,
                      sizeof(format), &format, nullptr),
                  "Read retrieved job format");
  if (format != QDMI_PROGRAM_FORMAT_CUSTOM2) {
    throw std::runtime_error("Scratch ID belongs to a different kind of job");
  }
  if (!Terminal(Check(raw))) {
    End(session, id);
  }
}
} // namespace

void Iteration_management_smoke(IQM_QDMI_Device_Session session,
                                const std::string &scratch,
                                const bool cleanup_only,
                                void (*check_circuits)(IQM_QDMI_Device_Session,
                                                       const std::string &,
                                                       bool)) {
  if (cleanup_only) {
    bool clean = true;
    for (const auto &id : Read_ids(scratch)) {
      try {
        Reconcile(session, id);
        Remove_id(scratch, id);
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        clean = false;
      }
    }
    if (!clean) {
      throw std::runtime_error("Unconfirmed IDs remain in the scratch file");
    }
    return;
  }

  std::ofstream ledger{scratch, std::ios::app};
  if (!ledger) {
    throw std::runtime_error("Cannot open scratch file before creation");
  }
  const auto owner = Create_job(session);
  Set_reservation(owner.get(),
                  R"({"max_duration_seconds":60,"idle_timeout_seconds":15})");
  const auto created = IQM_QDMI_device_job_submit(owner.get());
  // A malformed status can follow a confirmed creation. Save its ID even then.
  std::array<char, 37> buffer{};
  const int queried = IQM_QDMI_device_job_query_property(
      owner.get(), QDMI_DEVICE_JOB_PROPERTY_ID, buffer.size(), buffer.data(),
      nullptr);
  const std::string id{buffer.data()};
  if (queried == QDMI_SUCCESS && Canonical_id(id)) {
    ledger << id << '\n';
    ledger.close();
    if (!ledger) {
      throw std::runtime_error("Cannot persist confirmed reservation ID " + id);
    }
    std::cout << "Reservation " << id << '\n';
  }
  Require_success(created, "Create reservation");
  if (queried != QDMI_SUCCESS || !Canonical_id(id)) {
    throw std::runtime_error(
        "Creation did not return a canonical reservation ID");
  }
  const auto state = Check(owner.get());
  if (state != QDMI_JOB_STATUS_QUEUED && state != QDMI_JOB_STATUS_RUNNING) {
    throw std::runtime_error("Reservation ended before the smoke test");
  }
  if (check_circuits != nullptr) {
    std::cout << "Reservation observed "
              << (state == QDMI_JOB_STATUS_QUEUED ? "waiting" : "active")
              << " immediately before the first circuit submission\n";
    check_circuits(session, id, false);
  }
  End(session, id);
  if (!Terminal(Check(owner.get()))) {
    throw std::runtime_error("Owner did not observe the terminal reservation");
  }
  Remove_id(scratch, id);
  if (check_circuits != nullptr) {
    check_circuits(session, id, true);
  }
}
} // namespace iqm::test_support

#ifndef IQM_ITERATION_SMOKE_NO_MAIN
// NOLINTNEXTLINE(misc-const-correctness)
int main(const int argc, char **argv) {
  using namespace iqm::test_support;
  const bool cleanup =
      argc == 2 && std::string_view{argv[1]} == "--cleanup-only";
  const bool circuits = argc == 2 && std::string_view{argv[1]} == "--circuits";
  if (argc != 1 && !cleanup && !circuits) {
    return 1;
  }
  try {
    const auto *scratch = std::getenv("IQM_ITERATION_SCRATCH_FILE");
    if (scratch == nullptr || *scratch == '\0') {
      throw std::runtime_error(
          "Set IQM_ITERATION_SCRATCH_FILE to runner-local ID recovery state");
    }
    if (cleanup && Read_ids(scratch).empty()) {
      return 0;
    }
    Require_success(IQM_QDMI_device_initialize(), "Initialize device");
    {
      IQM_QDMI_Device_Session raw = nullptr;
      Require_success(IQM_QDMI_device_session_alloc(&raw), "Allocate session");
      const std::unique_ptr<IQM_QDMI_Device_Session_impl_d,
                            decltype(&IQM_QDMI_device_session_free)>
          session{raw, IQM_QDMI_device_session_free};
      constexpr std::string_view timeout = "timeout_ms:5000";
      Require_success(IQM_QDMI_device_session_set_parameter(
                          raw, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                          timeout.size() + 1, timeout.data()),
                      "Set session timeout");
      Require_success(IQM_QDMI_device_session_init(raw), "Initialize session");
      Iteration_management_smoke(raw, scratch, cleanup,
                                 circuits ? Check_circuits : nullptr);
    }
    Require_success(IQM_QDMI_device_finalize(), "Finalize device");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#endif
