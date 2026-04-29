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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iqm::test_support {

/**
 * @brief Exercise CurlHttpClient response handling with a synthetic status.
 *
 * This helper bypasses a live CURL handle and directly invokes the internal
 * response classification logic used by CurlHttpClient.
 *
 * @param response_code The HTTP status code to classify.
 * @param url The request URL used in generated log messages.
 * @param response The raw response body to parse and inspect.
 * @param use_debug_logging Whether non-success logs should be emitted at DEBUG
 * level instead of ERROR.
 * @return The mapped QDMI status code.
 */
int Handle_response_code_for_testing(int64_t response_code,
                                     const std::string &url,
                                     const std::string &response,
                                     bool use_debug_logging);

/**
 * @brief Make CurlHttpClient behave as if curl_easy_init failed.
 *
 * This helper is used to cover the GET and POST error paths that return
 * QDMI_ERROR_FATAL when no CURL handle can be created.
 */
void Enable_curl_easy_init_failure_for_testing();

/**
 * @brief Configure a synthetic HTTP response-code sequence for retry tests.
 *
 * This helper makes curl request execution succeed and returns the provided
 * response codes in order when CurlHttpClient queries the HTTP status.
 * Retry delays are recorded instead of sleeping.
 *
 * @param response_codes The sequence of HTTP status codes to return.
 */
void Enable_rate_limit_response_codes_for_testing(
    const std::vector<int64_t> &response_codes);

/**
 * @brief Return how many retry-delay calls were recorded in test mode.
 *
 * @return The number of recorded retry sleep calls.
 */
size_t Get_recorded_sleep_call_count_for_testing();

/**
 * @brief Restore the default libcurl hooks after a test override.
 */
void Reset_curl_api_hooks_for_testing();

} // namespace iqm::test_support
