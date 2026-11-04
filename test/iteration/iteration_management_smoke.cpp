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

#include "iqm_qdmi/iteration.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace iqm::test_support {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

bool Canonical_id(const std::string &id) {
  if (id.size() != 36)
    return false;
  for (size_t i = 0; i < id.size(); ++i) {
    const auto c = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  }
  return true;
}
bool Terminal(const IQM_QDMI_Iteration_Result &result) {
  const std::string_view state{result.state};
  return state == "completed" || state == "cancelled" || state == "failed";
}
uint64_t Budget(const Clock::time_point deadline) {
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - Clock::now());
  return static_cast<uint64_t>(std::clamp(remaining, 0ms, 10000ms).count());
}
void Report(IQM_QDMI_Iteration_Client client,
            const IQM_QDMI_Iteration_Result &result) {
  std::cout << "id=" << result.iteration_id << " state=" << result.state
            << " http=" << result.http_status << '\n'
            << IQM_QDMI_iteration_client_diagnostics(client) << '\n';
}
std::vector<std::string> Read_ids(const std::string &path) {
  std::ifstream input{path};
  if (!input && std::filesystem::exists(path))
    throw std::runtime_error("Cannot read scratch IDs");
  std::vector<std::string> ids;
  for (std::string id; std::getline(input, id);)
    if (!id.empty())
      ids.push_back(id);
  if (input.bad())
    throw std::runtime_error("Cannot read scratch IDs");
  return ids;
}
void Save_id(const std::string &path, const std::string &id) {
  std::ofstream file{path, std::ios::app};
  file << id << '\n' << std::flush;
  if (!file)
    throw std::runtime_error("Cannot persist confirmed iteration ID");
}
void Remove_id(const std::string &path, const std::string &id) {
  const auto ids = Read_ids(path);
  std::ofstream file{path, std::ios::trunc};
  for (const auto &entry : ids)
    if (entry != id)
      file << entry << '\n';
  file.flush();
  if (!file)
    throw std::runtime_error("Cannot update scratch IDs");
}

bool Cleanup(IQM_QDMI_Iteration_Client client, const std::string &id,
             const Clock::time_point deadline) {
  bool end_accepted = false;
  auto delay = 1s;
  while (Budget(deadline) > 0) {
    IQM_QDMI_Iteration_Result result{};
    const auto status =
        IQM_QDMI_iteration_get(client, id.c_str(), Budget(deadline), &result);
    Report(client, result);
    if (status == QDMI_SUCCESS && Terminal(result))
      return true;
    if (!end_accepted && Budget(deadline) > 0) {
      const auto ended =
          IQM_QDMI_iteration_end(client, id.c_str(), Budget(deadline), &result);
      Report(client, result);
      if (ended == QDMI_SUCCESS) {
        if (Terminal(result))
          return true;
        end_accepted = true;
        continue; // Inspect immediately after an accepted asynchronous end.
      }
    }
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero())
      break;
    std::this_thread::sleep_for(std::min(Clock::duration{delay}, remaining));
    delay = std::min(delay * 2, 5s);
  }
  std::cerr << "Termination unconfirmed for " << id << '\n';
  return false;
}
} // namespace

int Iteration_management_smoke(const bool cleanup_only) {
  try {
    const auto *scratch = std::getenv("IQM_ITERATION_SCRATCH_FILE");
    if (scratch == nullptr || *scratch == '\0') {
      std::cerr << "Set IQM_ITERATION_SCRATCH_FILE to runner-local ID recovery "
                   "state\n";
      return 1;
    }
    IQM_QDMI_Iteration_Client raw = nullptr;
    const auto allocated =
        IQM_QDMI_iteration_client_alloc(nullptr, nullptr, nullptr, 10000, &raw);
    if (allocated != QDMI_SUCCESS) {
      std::cerr << "Cannot allocate iteration client: " << allocated << '\n';
      return 1;
    }
    const std::unique_ptr<IQM_QDMI_Iteration_Client_impl,
                          decltype(&IQM_QDMI_iteration_client_free)>
        client{raw, IQM_QDMI_iteration_client_free};
    if (cleanup_only) {
      const auto deadline = Clock::now() + 30s;
      bool clean = true;
      for (const auto &id : Read_ids(scratch)) {
        if (!Canonical_id(id)) {
          std::cerr << "Invalid scratch ID; no request issued\n";
          clean = false;
        } else if (Budget(deadline) > 0 && Cleanup(raw, id, deadline))
          Remove_id(scratch, id);
        else
          clean = false;
      }
      return clean ? 0 : 1;
    }
    const char *qc = std::getenv("IQM_QC_ID");
    if (qc == nullptr || *qc == '\0')
      qc = std::getenv("IQM_QC_ALIAS");
    if (qc == nullptr || *qc == '\0')
      qc = "emerald:mock";
    IQM_QDMI_Iteration_Result result{};
    std::string id;
    bool success = false;
    try {
      const auto created =
          IQM_QDMI_iteration_create(raw, qc, 60, 60, 30000, &result);
      if (*result.iteration_id != '\0') {
        id = result.iteration_id;
        Save_id(scratch,
                id); // Persist even when create retained an ID on failure.
      }
      Report(raw, result);
      if (created == QDMI_SUCCESS) {
        const auto deadline = Clock::now() + 120s;
        auto delay = 1s;
        do {
          if (Budget(deadline) == 0)
            break;
          const auto inspected = IQM_QDMI_iteration_get(
              raw, id.c_str(), Budget(deadline), &result);
          Report(raw, result);
          if (inspected != QDMI_SUCCESS)
            break;
          if (std::string_view{result.state} == "active") {
            success = true;
            break;
          }
          if (Terminal(result))
            break;
          std::this_thread::sleep_for(
              std::min(Clock::duration{delay}, deadline - Clock::now()));
          delay = std::min(delay * 2, 5s);
        } while (Clock::now() < deadline);
      }
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      success = false;
    }
    if (!id.empty()) {
      if (Cleanup(raw, id, Clock::now() + 30s))
        Remove_id(scratch, id);
      else
        success = false;
    }
    return success ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
} // namespace iqm::test_support

#ifndef IQM_ITERATION_SMOKE_NO_MAIN
int main(const int argc, char **argv) {
  if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--cleanup-only"))
    return 1;
  try {
    return iqm::test_support::Iteration_management_smoke(argc == 2);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#endif
