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
 * @brief HTTP client used by the IQM QDMI device to talk to remote services.
 */

#pragma once

#include "iqm_qdmi/constants.h"

#include <chrono>
#include <cpr/bearer.h>
#include <cpr/body.h>
#include <cpr/connection_pool.h>
#include <cpr/cprtypes.h>
#include <cpr/response.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>

namespace iqm::http {

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
 * @brief Perform an HTTP GET request.
 *
 * Sends an HTTP GET request to the specified URL with bearer token
 * authentication. The request waits when the IQM Server API has reported the
 * remaining quota running low, and HTTP 429 responses are retried according to
 * the server's Retry-After header.
 *
 * @param url The target URL for the GET request.
 * @param bearer_token Bearer token used for authentication, if configured.
 * @param connection_pool Connection pool shared by the owning device session.
 * @param timeout Overall timeout for the request, any rate-limit wait, and any
 * rate-limit retries. A timeout shorter than the wait leaves no room for it,
 * so the request proceeds unthrottled and relies on the Retry-After path.
 * @return CPR response object.
 */
cpr::Response Get(const cpr::Url &url,
                  const std::optional<cpr::Bearer> &bearer_token,
                  const cpr::ConnectionPool &connection_pool,
                  std::chrono::milliseconds timeout = std::chrono::hours{1});

/**
 * @brief Perform an HTTP POST request.
 *
 * Sends an HTTP POST request to the specified URL with a JSON body. The
 * request automatically includes a JSON content type header and supports
 * additional custom headers. The request waits when the IQM Server API has
 * reported the remaining quota running low, and HTTP 429 responses are retried
 * according to the server's Retry-After header.
 *
 * @param url The target URL for the POST request.
 * @param bearer_token Bearer token used for authentication, if configured.
 * @param connection_pool Connection pool shared by the owning device session.
 * @param data The request body data.
 * @param additional_headers Additional HTTP headers to include.
 * @param timeout Overall timeout for the request, any rate-limit wait, and any
 * rate-limit retries. A timeout shorter than the wait leaves no room for it,
 * so the request proceeds unthrottled and relies on the Retry-After path.
 * @return CPR response object.
 */
cpr::Response Post(const cpr::Url &url,
                   const std::optional<cpr::Bearer> &bearer_token,
                   const cpr::ConnectionPool &connection_pool,
                   const cpr::Body &data,
                   const cpr::Header &additional_headers = {},
                   std::chrono::milliseconds timeout = std::chrono::hours{1});

/**
 * @brief Classify an HTTP response and log diagnostics.
 *
 * @return The mapped QDMI status code.
 */
QDMI_STATUS Handle_response(
    const cpr::Response &response,
    ERROR_LOG_POLICY error_log_policy = ERROR_LOG_POLICY::LOG_AS_ERROR);

namespace internal {
/**
 * @brief Clamp a logical timeout to the integer range used by a transport.
 *
 * CPR passes timeouts to libcurl as a `long`, which is only 32 bits on
 * Windows. The logical request budget remains unchanged; only an individual
 * transport attempt is clamped.
 *
 * @tparam TransportRep Integral representation accepted by the transport.
 * @param timeout Logical request timeout.
 * @return Timeout representable by the transport.
 */
template <typename TransportRep>
[[nodiscard]] constexpr auto
Clamp_timeout_for_transport(const std::chrono::milliseconds timeout)
    -> std::chrono::milliseconds {
  static_assert(std::is_integral_v<TransportRep>);
  using TimeoutRep = std::chrono::milliseconds::rep;
  if constexpr (std::numeric_limits<TransportRep>::digits >=
                std::numeric_limits<TimeoutRep>::digits) {
    return timeout;
  } else {
    constexpr auto transport_maximum = std::chrono::milliseconds{
        static_cast<TimeoutRep>(std::numeric_limits<TransportRep>::max())};
    return timeout > transport_maximum ? transport_maximum : timeout;
  }
}

/**
 * @brief Function hooks used to intercept HTTP calls and retry delays.
 *
 * Tests override these to exercise get()/post()/retry logic without a live
 * network connection or real time delays. Production code uses the defaults
 * installed by Get_hooks(), which perform real requests and real sleeps.
 */
struct Hooks {
  /// Hook for GET requests.
  std::function<cpr::Response(
      const cpr::Url &url, const std::optional<cpr::Bearer> &bearer_token,
      const cpr::ConnectionPool &connection_pool, const cpr::Header &headers,
      std::chrono::milliseconds timeout)>
      get;
  /// Hook for POST requests.
  std::function<cpr::Response(
      const cpr::Url &url, const std::optional<cpr::Bearer> &bearer_token,
      const cpr::ConnectionPool &connection_pool, const cpr::Header &headers,
      const cpr::Body &body, std::chrono::milliseconds timeout)>
      post;
  /// Hook for the retry backoff delay, given a delay in seconds.
  std::function<void(int)> sleep;
  /// Hook for the monotonic clock that drives request deadlines and the
  /// rate-limit window. Tests replace it with a clock the stubbed sleep
  /// advances, so waits are exact instead of racing the wall clock.
  std::function<std::chrono::steady_clock::time_point()> now;
};

/// Access the mutable, process-wide hook set.
Hooks &Get_hooks();

/// Restore the default (real) hooks. Used by tests to clean up after
/// themselves.
void Reset_hooks();

/**
 * @brief Forget the rate-limit quota reported by the IQM Server API.
 *
 * The quota belongs to the user account, so it is tracked once per process and
 * outlives any single session. Tests call this to keep that state from leaking
 * between them.
 */
void Reset_rate_limit_state();
} // namespace internal

} // namespace iqm::http
