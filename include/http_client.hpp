/*
 * Copyright (c) 2025 - 2026 IQM QDMI developers
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @file
 * @brief HTTP client interface for IQM QDMI device communication.
 */

#pragma once

#include <string>

namespace iqm {

/**
 * @brief Interface for HTTP client implementations.
 *
 * This interface defines the contract for HTTP clients used by the IQM QDMI
 * device to communicate with remote services. Implementations should handle
 * authentication, error handling, and proper HTTP protocol management.
 */
class IHttpClient {
public:
  IHttpClient() = default;
  IHttpClient(const IHttpClient &) = default;
  IHttpClient(IHttpClient &&) = default;
  IHttpClient &operator=(const IHttpClient &) = default;
  IHttpClient &operator=(IHttpClient &&) = default;
  virtual ~IHttpClient() = default;

  /**
   * @brief Perform an HTTP GET request.
   *
   * Sends an HTTP GET request to the specified URL with optional bearer token
   * authentication.
   *
   * @param url The target URL for the GET request.
   * @param bearer_token The bearer token for authentication (can be empty).
   * @param response Reference to string that will contain the response body.
   * @return HTTP status code or error code from qdmi/constants.h.
   */
  virtual int get(const std::string &url, const std::string &bearer_token,
                  std::string &response) = 0;

  /**
   * @brief Perform an HTTP POST request.
   *
   * Sends an HTTP POST request to the specified URL with optional data payload,
   * bearer token authentication, and additional headers.
   *
   * @param url The target URL for the POST request.
   * @param bearer_token The bearer token for authentication (can be empty).
   * @param response Reference to string that will contain the response body.
   * @param data The request body data (default: empty string).
   * @param extra_header Additional HTTP header to include (default: empty
   * string).
   * @return HTTP status code or error code from qdmi/constants.h.
   */
  virtual int post(const std::string &url, const std::string &bearer_token,
                   std::string &response, const std::string &data,
                   const std::string &extra_header) = 0;
};
} // namespace iqm
