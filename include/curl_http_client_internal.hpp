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
 * @brief Internal helpers for CurlHttpClient, shared between the
 * implementation and its unit tests.
 *
 * This header is **not** part of the public API.  It exists so that the test
 * tree can exercise internal logic (response-code mapping, retry loops, hook
 * injection) without the source file including anything from the test tree.
 */

#pragma once

#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <chrono>
#include <cstdint>
#include <curl/curl.h>
#include <curl/easy.h>
#include <string>
#include <thread>

namespace iqm::internal {

/**
 * @brief Logging policy used for non-success HTTP response handling.
 *
 * This enum controls whether response failures are surfaced at ERROR level for
 * required requests or downgraded to DEBUG for optional capability probes.
 */
enum class ERROR_LOG_POLICY : uint8_t {
  /// Log all errors at ERROR level (default for required requests)
  LOG_AS_ERROR,
  /// Log errors at DEBUG level (use for optional capability checks to avoid
  /// alarming users with expected failures)
  LOG_AS_DEBUG,
};

/// Number of retries to attempt after receiving HTTP 429.
constexpr uint8_t RATE_LIMIT_RETRY_COUNT = 5;

/**
 * @brief Result of a single CURL request attempt.
 */
struct Request_attempt_result {
  /// The result returned by curl_easy_perform.
  CURLcode curl_result = CURLE_OK;
  /// The HTTP response code observed for the completed request.
  int64_t response_code = 0;
};

/**
 * @brief Function hooks used to override selected libcurl entry points.
 *
 * Tests use these hooks to force specific failure paths without requiring a
 * live network interaction.
 */
struct Curl_api_hooks {
  /// Hook for curl_easy_init.
  CURL *(*easy_init)() = curl_easy_init;
  /// Hook for curl_easy_perform.
  CURLcode (*easy_perform)(CURL *) = curl_easy_perform;
};

/// Access the mutable curl hook set.
Curl_api_hooks &Get_curl_api_hooks();

/**
 * @brief Helper for policy-aware error logging.
 */
inline void Log_error(const ERROR_LOG_POLICY policy,
                      const std::string &message) {
  if (policy == ERROR_LOG_POLICY::LOG_AS_DEBUG) {
    LOG_DEBUG(message);
    return;
  }
  LOG_ERROR(message);
}

/**
 * @brief Classify an HTTP response code and log diagnostics.
 *
 * @return The mapped QDMI status code.
 */
int Handle_response_code(int64_t response_code, const std::string &url,
                         const std::string &response,
                         ERROR_LOG_POLICY error_log_policy);

/**
 * @brief Execute a configured request and retry on HTTP 429 responses.
 *
 * The supplied callback performs one request attempt and returns both the
 * libcurl result and the resulting HTTP response code.
 */
template <typename Perform_attempt>
int Perform_request_with_retries(const std::string &url, std::string &response,
                                 const ERROR_LOG_POLICY error_log_policy,
                                 Perform_attempt perform_attempt) {
  for (uint8_t attempt = 0; attempt <= RATE_LIMIT_RETRY_COUNT; ++attempt) {
    response.clear();
    const auto attempt_result = perform_attempt();

    if (attempt_result.curl_result != CURLE_OK) {
      Log_error(error_log_policy, "curl_easy_perform() failed: " +
                                      std::string(curl_easy_strerror(
                                          attempt_result.curl_result)));
      return QDMI_ERROR_FATAL;
    }

    if (attempt_result.response_code != 429 ||
        attempt == RATE_LIMIT_RETRY_COUNT) {
      return Handle_response_code(attempt_result.response_code, url, response,
                                  error_log_policy);
    }

    const int delay_seconds = 2 << attempt;
    LOG_DEBUG("Request to URL '" + url +
              "' hit HTTP 429 rate limiting; retrying in " +
              std::to_string(delay_seconds) + " second(s) (attempt " +
              std::to_string(attempt + 1) + "/" +
              std::to_string(RATE_LIMIT_RETRY_COUNT) + ")");
    std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
  }

  return QDMI_ERROR_FATAL;
}

} // namespace iqm::internal
