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

#include "http_client.hpp"

#include "http_client_internal.hpp"
#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <cerrno>
#include <chrono>
#include <cpr/api.h>
#include <cpr/bearer.h>
#include <cpr/body.h>
#include <cpr/cprtypes.h>
#include <cpr/error.h>
#include <cpr/redirect.h>
#include <cpr/response.h>
#include <cpr/session.h>
#include <cpr/ssl_options.h>
#include <cpr/timeout.h>
#include <cpr/user_agent.h>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace iqm::http {
namespace internal {

namespace {

cpr::Header Make_headers(const cpr::Header &additional_headers = {}) {
  cpr::Header headers;
  for (const auto &[key, value] : additional_headers) {
    headers.insert_or_assign(key, value);
  }
  return headers;
}

cpr::Header Make_json_headers(const cpr::Header &additional_headers) {
  auto headers = Make_headers(additional_headers);
  headers.emplace("Content-Type", "application/json");
  return headers;
}

void Apply_common_options(cpr::Session &session, const cpr::Url &url,
                          const std::optional<cpr::Bearer> &bearer_token,
                          const cpr::Header &headers) {
  session.SetUrl(url);
  session.SetHeader(headers);
  session.SetUserAgent(cpr::UserAgent{"QDMI-on-IQM"});
  session.SetTimeout(cpr::Timeout{std::chrono::seconds{3600}});
  session.SetRedirect(cpr::Redirect{true});
  session.SetVerifySsl(cpr::VerifySsl{true});
  if (bearer_token.has_value()) {
    session.SetBearer(*bearer_token);
  }
}

cpr::Response Default_get(const cpr::Url &url,
                          const std::optional<cpr::Bearer> &bearer_token,
                          const cpr::Header &headers) {
  cpr::Session session;
  Apply_common_options(session, url, bearer_token, headers);
  return session.Get();
}

cpr::Response Default_post(const cpr::Url &url,
                           const std::optional<cpr::Bearer> &bearer_token,
                           const cpr::Header &headers, const cpr::Body &body) {
  cpr::Session session;
  Apply_common_options(session, url, bearer_token, headers);
  session.SetBody(body);
  return session.Post();
}

void Default_sleep(const int seconds) {
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

/// Number of times to retry after receiving HTTP 429. Each retry waits for the
/// server-provided Retry-After duration.
constexpr uint8_t RATE_LIMIT_RETRY_COUNT = 10;
/// Conservative fallback for malformed test servers or proxies that strip
/// Retry-After from HTTP 429 responses. IQM's documented block duration is 30s.
constexpr int DEFAULT_RATE_LIMIT_RETRY_AFTER_SECONDS = 30;

int Retry_after_seconds(const cpr::Response &http_response) {
  const auto retry_after = http_response.header.find("Retry-After");
  if (retry_after == http_response.header.end()) {
    return DEFAULT_RATE_LIMIT_RETRY_AFTER_SECONDS;
  }

  errno = 0;
  char *end = nullptr;
  if (const auto seconds = std::strtol(retry_after->second.c_str(), &end, 10);
      end != retry_after->second.c_str() && *end == '\0' && errno != ERANGE &&
      seconds >= 0) {
    if (seconds > std::numeric_limits<int>::max()) {
      return std::numeric_limits<int>::max();
    }
    return static_cast<int>(seconds);
  }
  return DEFAULT_RATE_LIMIT_RETRY_AFTER_SECONDS;
}

void Log_error(const ERROR_LOG_POLICY policy, const std::string &message) {
  if (policy == ERROR_LOG_POLICY::LOG_AS_DEBUG) {
    LOG_DEBUG(message);
    return;
  }
  LOG_ERROR(message);
}

/// Execute a hooked request, obeying IQM Server API Retry-After headers for
/// HTTP 429 rate limiting.
template <typename Perform_attempt>
cpr::Response Perform_with_retries(const cpr::Url &url,
                                   Perform_attempt perform_attempt) {
  const auto &hooks = Get_hooks();
  for (uint8_t attempt = 0; attempt <= RATE_LIMIT_RETRY_COUNT; ++attempt) {
    const auto http_response = perform_attempt();

    if (http_response.error) {
      return http_response;
    }

    if (http_response.status_code != 429 || attempt == RATE_LIMIT_RETRY_COUNT) {
      return http_response;
    }

    const int delay_seconds = Retry_after_seconds(http_response);
    LOG_DEBUG("Request to URL '" + url.str() +
              "' hit HTTP 429 rate limiting; retrying after " +
              std::to_string(delay_seconds) +
              " second(s) from the Retry-After header (attempt " +
              std::to_string(attempt + 1) + "/" +
              std::to_string(RATE_LIMIT_RETRY_COUNT) + ")");
    hooks.sleep(delay_seconds);
  }

  cpr::Response response;
  response.url = url;
  response.error = cpr::Error{static_cast<int>(cpr::ErrorCode::UNKNOWN_ERROR),
                              "Rate-limit retry loop ended without a response"};
  return response;
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

} // namespace internal

QDMI_STATUS Handle_response(const cpr::Response &response,
                            const ERROR_LOG_POLICY error_log_policy) {
  if (response.error) {
    internal::Log_error(error_log_policy,
                        "Request failed: " + response.error.message);
    return QDMI_ERROR_FATAL;
  }

  const auto response_code = response.status_code;

  // Parse JSON response for IQM-specific errors and messages
  nlohmann::json json_response; // NOLINT(misc-include-cleaner)
  bool has_json = false;
  if (!response.text.empty()) {
    try {
      json_response = nlohmann::json::parse(response.text);
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
  std::string error_context = "Request to URL '" + response.url.str() +
                              "' failed with HTTP " +
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

  internal::Log_error(error_log_policy, error_context);

  // Log IQM-specific errors if present
  bool logged_structured_error = false;

  // Check for array of errors
  if (has_json && json_response.contains("errors") &&
      !json_response["errors"].is_null() &&
      json_response["errors"].is_array() && !json_response["errors"].empty()) {
    internal::Log_error(error_log_policy,
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
        internal::Log_error(error_log_policy, "  - " + error_msg);
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
      internal::Log_error(error_log_policy, "Error: " + error_msg);
      logged_structured_error = true;
    }
  }

  // Fall back to raw response if no structured errors are available
  if (!logged_structured_error && !response.text.empty()) {
    internal::Log_error(error_log_policy, "Response: " + response.text);
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

cpr::Response Get(const cpr::Url &url,
                  const std::optional<cpr::Bearer> &bearer_token) {
  LOG_INFO("Performing GET request to " + url.str());
  const auto &hooks = internal::Get_hooks();
  const auto headers = internal::Make_headers();
  return internal::Perform_with_retries(
      url, [&]() { return hooks.get(url, bearer_token, headers); });
}

cpr::Response Get_optional(const cpr::Url &url,
                           const std::optional<cpr::Bearer> &bearer_token) {
  LOG_INFO("Performing GET request to " + url.str());
  const auto &hooks = internal::Get_hooks();
  const auto headers = internal::Make_headers();
  return internal::Perform_with_retries(
      url, [&]() { return hooks.get(url, bearer_token, headers); });
}

cpr::Response Post(const cpr::Url &url,
                   const std::optional<cpr::Bearer> &bearer_token,
                   const cpr::Body &data,
                   const cpr::Header &additional_headers) {
  LOG_INFO("Performing POST request to " + url.str());
  if (const auto &data_str = data.str(); !data_str.empty()) {
    LOG_DEBUG("POST data: " + data_str);
  }
  const auto &hooks = internal::Get_hooks();
  const auto headers = internal::Make_json_headers(additional_headers);
  return internal::Perform_with_retries(
      url, [&]() { return hooks.post(url, bearer_token, headers, data); });
}

} // namespace iqm::http
