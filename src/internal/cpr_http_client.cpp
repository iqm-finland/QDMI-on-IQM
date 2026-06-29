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

#include "cpr_http_client.hpp"

#include "cpr_http_client_internal.hpp"
#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace iqm {
namespace internal {

Cpr_api_hooks &Get_cpr_api_hooks() {
  static Cpr_api_hooks cpr_api_hooks{};
  return cpr_api_hooks;
}

int Handle_response_code(const int64_t response_code, const std::string &url,
                         const std::string &response,
                         const ERROR_LOG_POLICY error_log_policy) {
  // Parse JSON response for IQM-specific errors and messages
  nlohmann::json json_response;
  bool has_json = false;
  if (!response.empty()) {
    try {
      json_response = nlohmann::json::parse(response);
      has_json = true;
    } catch (const nlohmann::json::exception &) {
      LOG_DEBUG("Response is not valid JSON");
    }
  }

  // Handle response codes according to REST conventions
  if (response_code >= 200 && response_code < 300) {
    // 2xx Success
    // Log IQM messages if present
    if (has_json && json_response.contains("messages") &&
        json_response["messages"].is_array() &&
        !json_response["messages"].empty()) {
      LOG_DEBUG("Response contains " +
                std::to_string(json_response["messages"].size()) +
                " message(s):");
      for (const auto &message : json_response["messages"]) {
        std::string msg_text;
        if (message.contains("message") && message["message"].is_string()) {
          msg_text = message["message"].get<std::string>();
        }
        if (!msg_text.empty()) {
          LOG_DEBUG("  - " + msg_text);
        }
      }
    }

    LOG_INFO("Request successful (HTTP " + std::to_string(response_code) + ")");
    return QDMI_SUCCESS;
  }

  // Handle error responses (4xx client errors, 5xx server errors, etc.)
  std::string error_context = "Request to URL '" + url + "' failed with HTTP " +
                              std::to_string(response_code);

  if (response_code >= 400 && response_code < 500) {
    // 4xx Client Error
    error_context += " (Client Error)";
  } else if (response_code >= 500 && response_code < 600) {
    // 5xx Server Error
    error_context += " (Server Error)";
  } else if (response_code >= 300 && response_code < 400) {
    // 3xx Redirection (unexpected since we follow redirects)
    error_context += " (Unexpected Redirect)";
  } else {
    // Other non-success codes
    error_context += " (Unexpected Response Code)";
  }

  Log_error(error_log_policy, error_context);

  // Log IQM-specific errors if present
  bool logged_structured_error = false;

  // Check for array of errors
  if (has_json && json_response.contains("errors") &&
      !json_response["errors"].is_null() &&
      json_response["errors"].is_array() && !json_response["errors"].empty()) {
    Log_error(error_log_policy,
              "Response contains " +
                  std::to_string(json_response["errors"].size()) +
                  " error(s):");
    for (const auto &error : json_response["errors"]) {
      std::string error_msg;
      if (error.contains("error_code") && error["error_code"].is_string()) {
        error_msg = "[" + error["error_code"].get<std::string>() + "] ";
      }
      if (error.contains("message") && error["message"].is_string()) {
        error_msg += error["message"].get<std::string>();
      }
      if (!error_msg.empty()) {
        Log_error(error_log_policy, "  - " + error_msg);
      }
    }
    logged_structured_error = true;
  }

  // Check for single error_code and message at root level
  if (has_json && !logged_structured_error &&
      (json_response.contains("error_code") ||
       json_response.contains("message"))) {
    std::string error_msg;
    if (json_response.contains("error_code") &&
        json_response["error_code"].is_string()) {
      error_msg = "[" + json_response["error_code"].get<std::string>() + "] ";
    }
    if (json_response.contains("message") &&
        json_response["message"].is_string()) {
      error_msg += json_response["message"].get<std::string>();
    }
    if (!error_msg.empty()) {
      Log_error(error_log_policy, "Error: " + error_msg);
      logged_structured_error = true;
    }
  }

  // Fall back to raw response if no structured errors are available
  if (!logged_structured_error && !response.empty()) {
    Log_error(error_log_policy, "Response: " + response);
  }

  // Log IQM messages if present (might contain additional context)
  if (has_json && json_response.contains("messages") &&
      json_response["messages"].is_array() &&
      !json_response["messages"].empty()) {
    LOG_DEBUG("Response contains " +
              std::to_string(json_response["messages"].size()) +
              " additional message(s):");
    for (const auto &message : json_response["messages"]) {
      std::string msg_text;
      if (message.contains("message") && message["message"].is_string()) {
        msg_text = message["message"].get<std::string>();
      }
      if (!msg_text.empty()) {
        LOG_DEBUG("  - " + msg_text);
      }
    }
  }

  // Map HTTP status codes to QDMI error codes
  if (response_code == 401 || response_code == 403) {
    return QDMI_ERROR_PERMISSIONDENIED;
  }
  if (response_code == 404) {
    return QDMI_ERROR_NOTFOUND;
  }
  if (response_code == 408 || response_code == 504) {
    return QDMI_ERROR_TIMEOUT;
  }
  if (response_code == 400 || (response_code >= 402 && response_code < 500)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }

  return QDMI_ERROR_FATAL;
}

namespace {

int Perform_get_request(const std::string &url, const std::string &bearer_token,
                        std::string &response,
                        const ERROR_LOG_POLICY error_log_policy) {
  LOG_INFO("Performing GET request to " + url);

  cpr::Header headers;
  headers.emplace("User-Agent", "IQM QDMI C++ API");
  if (!bearer_token.empty()) {
    headers.emplace("Authorization", bearer_token);
  }

  cpr::Timeout timeout{std::chrono::seconds{3600}};
  cpr::Redirect redirect{true};
  cpr::VerifySsl verify_ssl{true};

  auto &cpr_api_hooks = Get_cpr_api_hooks();

  const auto ret = Perform_request_with_retries(
      url, response, error_log_policy, [&]() -> Request_attempt_result {
        cpr::Response r = cpr_api_hooks.get(cpr::Url{url}, headers, timeout,
                                            redirect, verify_ssl);
        response = r.text;
        return {.error_code = r.error.code,
                .error_message = r.error.message,
                .response_code = r.status_code};
      });
  return ret;
}

} // namespace
} // namespace internal

int CprHttpClient::get(const std::string &url, const std::string &bearer_token,
                       std::string &response) {
  return internal::Perform_get_request(
      url, bearer_token, response, internal::ERROR_LOG_POLICY::LOG_AS_ERROR);
}

int CprHttpClient::get_optional(const std::string &url,
                                const std::string &bearer_token,
                                std::string &response) {
  return internal::Perform_get_request(
      url, bearer_token, response, internal::ERROR_LOG_POLICY::LOG_AS_DEBUG);
}

int CprHttpClient::post(const std::string &url, const std::string &bearer_token,
                        std::string &response, const std::string &data,
                        const std::string &extra_header) {
  LOG_INFO("Performing POST request to " + url);

  cpr::Header headers;
  headers.emplace("User-Agent", "IQM QDMI C++ API");
  headers.emplace("Content-Type", "application/json");
  if (!bearer_token.empty()) {
    headers.emplace("Authorization", bearer_token);
  }
  if (!extra_header.empty()) {
    const size_t colon_pos = extra_header.find(':');
    if (colon_pos != std::string::npos) {
      std::string key = extra_header.substr(0, colon_pos);
      std::string val = extra_header.substr(colon_pos + 1);
      auto trim = [](std::string &s) {
        s.erase(s.begin(),
                std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                  return !std::isspace(ch);
                }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
                s.end());
      };
      trim(key);
      trim(val);
      headers.emplace(std::move(key), std::move(val));
    }
  }

  cpr::Timeout timeout{std::chrono::seconds{3600}};
  cpr::Redirect redirect{true};
  cpr::VerifySsl verify_ssl{true};

  if (!data.empty()) {
    LOG_DEBUG("POST data: " + data);
  }

  auto &cpr_api_hooks = internal::Get_cpr_api_hooks();

  const auto ret = internal::Perform_request_with_retries(
      url, response, internal::ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() -> internal::Request_attempt_result {
        cpr::Response r =
            cpr_api_hooks.post(cpr::Url{url}, headers, cpr::Body{data}, timeout,
                               redirect, verify_ssl);
        response = r.text;
        return {.error_code = r.error.code,
                .error_message = r.error.message,
                .response_code = r.status_code};
      });
  return ret;
}

} // namespace iqm
