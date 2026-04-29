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

#include <cstdint>
#include <curl/curl.h>
#include <string>

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
 * @brief Override the CURL entry points used by CurlHttpClient in tests.
 *
 * Passing nullptr for a hook restores the default libcurl function.
 *
 * @param easy_init Replacement for curl_easy_init.
 * @param easy_perform Replacement for curl_easy_perform.
 */
void Set_curl_api_hooks_for_testing(CURL *(*easy_init)(),
                                    CURLcode (*easy_perform)(CURL *));

/**
 * @brief Restore the default libcurl hooks after a test override.
 */
void Reset_curl_api_hooks_for_testing();

} // namespace iqm::test_support
