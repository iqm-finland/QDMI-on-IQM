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

/** @file
 * @brief Internal test seam for iqm::http, shared between the implementation
 * and its unit tests.
 *
 * This header is **not** part of the public API. It deliberately does not
 * expose any transport-library types (e.g. CPR types): hooks are expressed
 * purely in terms of plain strings and status codes, so tests never need to
 * know which HTTP library backs iqm::http.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace iqm::http::internal {

/**
 * @brief Logging policy used for non-success HTTP response handling.
 */
enum class ERROR_LOG_POLICY : uint8_t {
  /// Log all errors at ERROR level (default for required requests)
  LOG_AS_ERROR,
  /// Log errors at DEBUG level
  LOG_AS_DEBUG,
};

/**
 * @brief Outcome of a single low-level HTTP attempt.
 *
 * Deliberately decoupled from any particular HTTP transport library.
 */
struct Raw_response {
  /// The HTTP status code, or 0 if the request failed at the transport level
  /// (e.g. couldn't connect).
  int64_t status_code = 0;
  /// The response body (empty on transport-level failure).
  std::string body;
  /// Describes the transport-level failure; only set when status_code == 0.
  std::string error_message;
};

/**
 * @brief Function hooks used to intercept HTTP calls and retry delays.
 *
 * Tests override these to exercise get()/post()/retry logic without a live
 * network connection or real time delays. Production code uses the defaults
 * installed by Get_hooks(), which perform real requests and real sleeps.
 */
struct Hooks {
  /// Hook for GET requests.
  std::function<Raw_response(const std::string &url,
                             const std::string &bearer_token)>
      get;
  /// Hook for POST requests.
  std::function<Raw_response(
      const std::string &url, const std::string &bearer_token,
      const std::string &data, const std::string &extra_header)>
      post;
  /// Hook for the retry backoff delay, given a delay in seconds.
  std::function<void(int)> sleep;
};

/// Access the mutable, process-wide hook set.
Hooks &Get_hooks();

/// Restore the default (real) hooks. Used by tests to clean up after
/// themselves.
void Reset_hooks();

/**
 * @brief Classify an HTTP response code and log diagnostics.
 *
 * @return The mapped QDMI status code.
 */
int Handle_response_code(int64_t response_code, const std::string &url,
                         const std::string &response,
                         ERROR_LOG_POLICY error_log_policy);

} // namespace iqm::http::internal
