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
 *
 * This is the only translation unit that depends on CPR. Everything outside
 * of this file (including tests) interacts with iqm::http purely through
 * plain strings and status codes.
 */

#include "http_client.hpp"

#include "http_client_internal.hpp"
#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cpr/api.h>
#include <cpr/body.h>
#include <cpr/cprtypes.h>
#include <cpr/error.h>
#include <cpr/redirect.h>
#include <cpr/response.h>
#include <cpr/ssl_options.h>
#include <cpr/timeout.h>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string>
#include <thread>
#include <utility>

namespace iqm::http {
namespace internal {

namespace {

/// Split a "Key: Value" header string into a trimmed key/value pair. Returns
/// an empty key if no colon separator is present.
std::pair<std::string, std::string>
Split_header(const std::string &extra_header) {
  const size_t colon_pos = extra_header.find(':');
  if (colon_pos == std::string::npos) {
    return {};
  }
  auto trim = [](std::string s) {
    s.erase(s.begin(), std::ranges::find_if(s, [](const unsigned char ch) {
              return std::isspace(ch) == 0;
            }));
    s.erase(std::ranges::find_if(
                s | std::views::reverse,
                [](const unsigned char ch) { return std::isspace(ch) == 0; })
                .base(),
            s.end());
    return s;
  };
  return {trim(extra_header.substr(0, colon_pos)),
          trim(extra_header.substr(colon_pos + 1))};
}

Raw_response Make_raw_response(const cpr::Response &response) {
  if (response.error.code != cpr::ErrorCode::OK) {
    return {
        .status_code = 0, .body = {}, .error_message = response.error.message};
  }
  return {.status_code = response.status_code,
          .body = response.text,
          .error_message = {}};
}

Raw_response Default_get(const std::string &url,
                         const std::string &bearer_token) {
  cpr::Header headers;
  headers.emplace("User-Agent", "IQM QDMI C++ API");
  if (!bearer_token.empty()) {
    headers.emplace("Authorization", bearer_token);
  }
  return Make_raw_response(cpr::Get(cpr::Url{url}, headers,
                                    cpr::Timeout{std::chrono::seconds{3600}},
                                    cpr::Redirect{true}, cpr::VerifySsl{true}));
}

Raw_response Default_post(const std::string &url,
                          const std::string &bearer_token,
                          const std::string &data,
                          const std::string &extra_header) {
  cpr::Header headers;
  headers.emplace("User-Agent", "IQM QDMI C++ API");
  headers.emplace("Content-Type", "application/json");
  if (!bearer_token.empty()) {
    headers.emplace("Authorization", bearer_token);
  }
  if (!extra_header.empty()) {
    auto [key, value] = Split_header(extra_header);
    if (!key.empty()) {
      headers.emplace(std::move(key), std::move(value));
    }
  }
  return Make_raw_response(cpr::Post(cpr::Url{url}, headers, cpr::Body{data},
                                     cpr::Timeout{std::chrono::seconds{3600}},
                                     cpr::Redirect{true},
                                     cpr::VerifySsl{true}));
}

void Default_sleep(const int seconds) {
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

/// Number of retries to attempt after receiving HTTP 429.
constexpr uint8_t RATE_LIMIT_RETRY_COUNT = 5;

void Log_error(const ERROR_LOG_POLICY policy, const std::string &message) {
  if (policy == ERROR_LOG_POLICY::LOG_AS_DEBUG) {
    LOG_DEBUG(message);
    return;
  }
  LOG_ERROR(message);
}

/// Execute a hooked request and retry on HTTP 429 responses.
template <typename Perform_attempt>
int Perform_with_retries(const std::string &url, std::string &response,
                         const ERROR_LOG_POLICY error_log_policy,
                         Perform_attempt perform_attempt) {
  auto &hooks = Get_hooks();
  for (uint8_t attempt = 0; attempt <= RATE_LIMIT_RETRY_COUNT; ++attempt) {
    const auto raw = perform_attempt();

    if (raw.status_code == 0) {
      Log_error(error_log_policy, "Request failed: " + raw.error_message);
      response.clear();
      return QDMI_ERROR_FATAL;
    }

    if (raw.status_code != 429 || attempt == RATE_LIMIT_RETRY_COUNT) {
      response = raw.body;
      return Handle_response_code(raw.status_code, url, raw.body,
                                  error_log_policy);
    }

    const int delay_seconds = 2 << attempt;
    LOG_DEBUG("Request to URL '" + url +
              "' hit HTTP 429 rate limiting; retrying in " +
              std::to_string(delay_seconds) + " second(s) (attempt " +
              std::to_string(attempt + 1) + "/" +
              std::to_string(RATE_LIMIT_RETRY_COUNT) + ")");
    hooks.sleep(delay_seconds);
  }

  response.clear();
  return QDMI_ERROR_FATAL;
}

} // namespace

Hooks &Get_hooks() {
  static Hooks hooks{
      .get = Default_get, .post = Default_post, .sleep = Default_sleep};
  return hooks;
}

void Reset_hooks() {
  auto &[get, post, sleep] = Get_hooks();
  get = Default_get;
  post = Default_post;
  sleep = Default_sleep;
}

int Handle_response_code(const int64_t response_code, const std::string &url,
                         const std::string &response,
                         const ERROR_LOG_POLICY error_log_policy) {
  // Parse JSON response for IQM-specific errors and messages
  nlohmann::json json_response; // NOLINT(misc-include-cleaner)
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

} // namespace internal

int Get(const std::string &url, const std::string &bearer_token,
        std::string &response) {
  LOG_INFO("Performing GET request to " + url);
  const auto &hooks = internal::Get_hooks();
  return internal::Perform_with_retries(
      url, response, internal::ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() { return hooks.get(url, bearer_token); });
}

int Get_optional(const std::string &url, const std::string &bearer_token,
                 std::string &response) {
  LOG_INFO("Performing GET request to " + url);
  const auto &hooks = internal::Get_hooks();
  return internal::Perform_with_retries(
      url, response, internal::ERROR_LOG_POLICY::LOG_AS_DEBUG,
      [&]() { return hooks.get(url, bearer_token); });
}

int Post(const std::string &url, const std::string &bearer_token,
         std::string &response, const std::string &data,
         const std::string &extra_header) {
  LOG_INFO("Performing POST request to " + url);
  if (!data.empty()) {
    LOG_DEBUG("POST data: " + data);
  }
  const auto &hooks = internal::Get_hooks();
  return internal::Perform_with_retries(
      url, response, internal::ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() { return hooks.post(url, bearer_token, data, extra_header); });
}

} // namespace iqm::http
