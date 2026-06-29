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

/** @file
 * @brief Test-support function definitions for CprHttpClient unit tests.
 *
 * These helpers wrap internal CprHttpClient logic so that tests can exercise
 * response classification, retry behavior, and hook injection without needing
 * a live network connection.
 */

#include "cpr_http_client_test_support.hpp"

#include "cpr_http_client_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iqm::test_support {

int Handle_response_code_for_testing(const int64_t response_code,
                                     const std::string &url,
                                     const std::string &response,
                                     const bool use_debug_logging) {
  return internal::Handle_response_code(
      response_code, url, response,
      use_debug_logging ? internal::ERROR_LOG_POLICY::LOG_AS_DEBUG
                        : internal::ERROR_LOG_POLICY::LOG_AS_ERROR);
}

void Enable_cpr_request_failure_for_testing() {
  auto &cpr_api_hooks = internal::Get_cpr_api_hooks();
  cpr_api_hooks.get = [](const cpr::Url &, const cpr::Header &,
                         const cpr::Timeout &, const cpr::Redirect &,
                         const cpr::VerifySsl &) {
    cpr::Response r;
    r.error.code = cpr::ErrorCode::COULDNT_CONNECT;
    r.error.message = "Failed to connect to host";
    r.status_code = 0;
    return r;
  };
  cpr_api_hooks.post = [](const cpr::Url &, const cpr::Header &,
                          const cpr::Body &, const cpr::Timeout &,
                          const cpr::Redirect &, const cpr::VerifySsl &) {
    cpr::Response r;
    r.error.code = cpr::ErrorCode::COULDNT_CONNECT;
    r.error.message = "Failed to connect to host";
    r.status_code = 0;
    return r;
  };
}

Retry_test_result
Retry_response_codes_for_testing(const std::vector<int64_t> &response_codes,
                                 const std::string &url,
                                 const bool use_debug_logging) {
  size_t next_response_code_index = 0;
  std::string response;
  const auto status_code = internal::Perform_request_with_retries(
      url, response,
      use_debug_logging ? internal::ERROR_LOG_POLICY::LOG_AS_DEBUG
                        : internal::ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() -> internal::Request_attempt_result {
        if (next_response_code_index >= response_codes.size()) {
          return {.error_code = cpr::ErrorCode::OK,
                  .error_message = "",
                  .response_code = 0};
        }
        const auto response_code = response_codes[next_response_code_index];
        next_response_code_index++;
        return {.error_code = cpr::ErrorCode::OK,
                .error_message = "",
                .response_code = response_code};
      });
  // Retries = total attempts - 1 (the first call is not a retry).
  const size_t retry_count =
      next_response_code_index > 0 ? next_response_code_index - 1 : 0;
  return Retry_test_result{.status_code = status_code,
                           .sleep_call_count = retry_count};
}

void Reset_cpr_api_hooks_for_testing() {
  auto &cpr_api_hooks = internal::Get_cpr_api_hooks();
  cpr_api_hooks.get = [](const cpr::Url &url, const cpr::Header &headers,
                         const cpr::Timeout &timeout,
                         const cpr::Redirect &redirect,
                         const cpr::VerifySsl &verify_ssl) {
    return cpr::Get(url, headers, timeout, redirect, verify_ssl);
  };
  cpr_api_hooks.post = [](const cpr::Url &url, const cpr::Header &headers,
                          const cpr::Body &body, const cpr::Timeout &timeout,
                          const cpr::Redirect &redirect,
                          const cpr::VerifySsl &verify_ssl) {
    return cpr::Post(url, headers, body, timeout, redirect, verify_ssl);
  };
}

} // namespace iqm::test_support
