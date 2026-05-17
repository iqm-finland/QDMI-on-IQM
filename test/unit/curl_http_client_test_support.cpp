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
 * @brief Test-support function definitions for CurlHttpClient unit tests.
 *
 * These helpers wrap internal CurlHttpClient logic so that tests can exercise
 * response classification, retry behavior, and hook injection without needing
 * a live network connection.
 */

#include "curl_http_client_test_support.hpp"

#include "curl_http_client_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <curl/easy.h>
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

namespace {
void Set_curl_api_hooks_for_testing(CURL *(*easy_init)(),
                                    CURLcode (*easy_perform)(CURL *)) {
  auto &curl_api_hooks = internal::Get_curl_api_hooks();
  curl_api_hooks.easy_init = easy_init != nullptr ? easy_init : curl_easy_init;
  curl_api_hooks.easy_perform =
      easy_perform != nullptr ? easy_perform : curl_easy_perform;
}
} // namespace

void Enable_curl_easy_init_failure_for_testing() {
  Set_curl_api_hooks_for_testing(internal::Fail_curl_easy_init, nullptr);
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
          return {.curl_result = CURLE_OK, .response_code = 0};
        }
        const auto response_code = response_codes[next_response_code_index];
        next_response_code_index++;
        return {.curl_result = CURLE_OK, .response_code = response_code};
      });
  // Retries = total attempts - 1 (the first call is not a retry).
  const size_t retry_count =
      next_response_code_index > 0 ? next_response_code_index - 1 : 0;
  return Retry_test_result{.status_code = status_code,
                           .sleep_call_count = retry_count};
}

void Reset_curl_api_hooks_for_testing() {
  Set_curl_api_hooks_for_testing(nullptr, nullptr);
}

} // namespace iqm::test_support
