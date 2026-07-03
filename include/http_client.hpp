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
 * @brief HTTP client used by the IQM QDMI device to talk to remote services.
 */

#pragma once

#include <string>

namespace iqm::http {

/**
 * @brief Perform an HTTP GET request.
 *
 * Sends an HTTP GET request to the specified URL with bearer token
 * authentication, retrying on HTTP 429 responses.
 *
 * @param url The target URL for the GET request.
 * @param bearer_token The bearer token for authentication (can be empty).
 * @param response Reference to string that will contain the response body.
 * @return QDMI_SUCCESS on success, otherwise the mapped QDMI error code.
 */
int get(const std::string &url, const std::string &bearer_token,
        std::string &response);

/**
 * @brief Perform an optional HTTP GET request.
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
                 std::string &response);

/**
 * @brief Perform an HTTP POST request.
 *
 * Sends an HTTP POST request to the specified URL with a JSON body. The
 * request automatically includes a JSON content type header and supports one
 * additional custom header, retrying on HTTP 429 responses.
 *
 * @param url The target URL for the POST request.
 * @param bearer_token The bearer token for authentication (can be empty).
 * @param response Reference to string that will contain the response body.
 * @param data The request body data.
 * @param extra_header Additional HTTP header to include, formatted as
 * "Key: Value" (can be empty).
 * @return QDMI_SUCCESS on success, otherwise the mapped QDMI error code.
 */
int post(const std::string &url, const std::string &bearer_token,
         std::string &response, const std::string &data,
         const std::string &extra_header);

} // namespace iqm::http
