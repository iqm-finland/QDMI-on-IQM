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
 * @brief CPR-based HTTP client implementation for IQM QDMI device
 * communication.
 */

#pragma once

#include "http_client.hpp"

#include <string>

namespace iqm {

/**
 * @brief HTTP client implementation using the CPR library.
 *
 * This class provides a concrete implementation of the IHttpClient interface
 * using the CPR library for HTTP communication. It supports GET and POST
 * requests with authentication, SSL verification, and timeout handling.
 */
class CprHttpClient final : public IHttpClient {
public:
  /**
   * @brief Perform an HTTP GET request using CPR.
   *
   * Sends an HTTP GET request to the specified URL with bearer token
   * authentication.
   *
   * @param url The target URL for the GET request.
   * @param bearer_token The bearer token for authentication (can be empty).
   * @param response Reference to string that will contain the response body.
   * @return QDMI_SUCCESS on success, QDMI_ERROR_FATAL on failure.
   */
  int get(const std::string &url, const std::string &bearer_token,
          std::string &response) override;

  /**
   * @brief Perform an optional HTTP GET request using CPR.
   *
   * Behaves like get(), but downgrades non-success logging for capability
   * probes where missing endpoints are expected.
   *
   * @param url The target URL for the GET request.
   * @param bearer_token The bearer token for authentication (can be empty).
   * @param response Reference to string that will contain the response body.
   * @return QDMI_SUCCESS on success, otherwise the mapped QDMI error code.
   */
  int get_optional(const std::string &url, const std::string &bearer_token,
                   std::string &response) override;

  /**
   * @brief Perform an HTTP POST request using CPR.
   *
   * Sends an HTTP POST request to the specified URL with optional data payload.
   * The request automatically includes JSON content type header and supports
   * additional custom headers.
   *
   * @param url The target URL for the POST request.
   * @param bearer_token The bearer token for authentication (can be empty).
   * @param response Reference to string that will contain the response body.
   * @param data The request body data (default: empty string).
   * @param extra_header Additional HTTP header to include (default: empty
   * string).
   * @return QDMI_SUCCESS on success, QDMI_ERROR_FATAL on failure.
   */
  int post(const std::string &url, const std::string &bearer_token,
           std::string &response, const std::string &data,
           const std::string &extra_header) override;
};
} // namespace iqm
