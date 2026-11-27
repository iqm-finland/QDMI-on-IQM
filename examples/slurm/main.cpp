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

#include "reservation.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

// NOLINTNEXTLINE(misc-const-correctness)
int main(const int argc, char **argv) {
  using namespace iqm::slurm;
  const bool cleanup =
      argc == 2 && std::string_view{argv[1]} == "--cleanup-only";
  const bool circuits = argc == 2 && std::string_view{argv[1]} == "--circuits";
  if (!cleanup && !circuits) {
    std::cerr << "Usage: slurm_reservation --circuits | --cleanup-only\n";
    return 1;
  }
  try {
    const auto *scratch = std::getenv("IQM_ITERATION_SCRATCH_FILE");
    if (scratch == nullptr || *scratch == '\0') {
      throw std::runtime_error(
          "Set IQM_ITERATION_SCRATCH_FILE to a shared recovery file");
    }
    if (cleanup && (!std::filesystem::exists(scratch) ||
                    std::filesystem::file_size(scratch) == 0)) {
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
      if (cleanup) {
        Cleanup(raw, scratch);
      } else {
        Run_circuits(raw, scratch);
      }
    }
    Require_success(IQM_QDMI_device_finalize(), "Finalize device");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
