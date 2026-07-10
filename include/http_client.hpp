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

#include "cpr/body.h"
#include "iqm_qdmi/constants.h"

#include <cpr/bearer.h>
#include <cpr/cprtypes.h>
#include <cpr/response.h>
#include <cstdint>
#include <optional>
#include <string>

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
 * authentication. HTTP 429 responses are retried according to the server's
 * Retry-After header.
 *
 * @param url The target URL for the GET request.
 * @param bearer_token Bearer token used for authentication, if configured.
 * @return CPR response object.
 */
cpr::Response Get(const cpr::Url &url,
                  const std::optional<cpr::Bearer> &bearer_token);

/**
 * @brief Perform an optional HTTP GET request.
 *
 * Behaves like Get(), but downgrades non-success logging for capability
 * probes where missing endpoints are expected.
 *
 * @param url The target URL for the GET request.
 * @param bearer_token Bearer token used for authentication, if configured.
 * @return CPR response object.
 */
cpr::Response Get_optional(const cpr::Url &url,
                           const std::optional<cpr::Bearer> &bearer_token);

/**
 * @brief Perform an HTTP POST request.
 *
 * Sends an HTTP POST request to the specified URL with a JSON body. The
 * request automatically includes a JSON content type header and supports
 * additional custom headers. HTTP 429 responses are retried according to the
 * server's Retry-After header.
 *
 * @param url The target URL for the POST request.
 * @param bearer_token Bearer token used for authentication, if configured.
 * @param data The request body data.
 * @param additional_headers Additional HTTP headers to include.
 * @return CPR response object.
 */
cpr::Response Post(const cpr::Url &url,
                   const std::optional<cpr::Bearer> &bearer_token,
                   const cpr::Body &data,
                   const cpr::Header &additional_headers = {});

/**
 * @brief Classify an HTTP response and log diagnostics.
 *
 * @return The mapped QDMI status code.
 */
QDMI_STATUS Handle_response(
    const cpr::Response &response,
    ERROR_LOG_POLICY error_log_policy = ERROR_LOG_POLICY::LOG_AS_ERROR);

} // namespace iqm::http
