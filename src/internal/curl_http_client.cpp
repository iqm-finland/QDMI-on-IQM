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
 * @brief cURL-based HTTP client implementation for IQM QDMI device
 * communication.
 */

#include "curl_http_client.hpp"

#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <curl/easy.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>
#include <vector>

namespace iqm {

namespace test_support {

/**
 * @brief Outcome captured while exercising retry behavior in tests.
 */
struct Retry_test_result {
  /// The final QDMI status code returned by the retry helper.
  int status_code;
  /// The number of retry-delay calls that would have been performed.
  size_t sleep_call_count;
};

} // namespace test_support

namespace {
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
constexpr uint8_t RATE_LIMIT_RETRY_COUNT = 3;

/// Delay in seconds between consecutive HTTP 429 retry attempts.
constexpr uint8_t RATE_LIMIT_RETRY_DELAY_SECONDS = 2;

/**
 * @brief Result of a single CURL request attempt.
 */
struct Request_attempt_result {
  /// The result returned by curl_easy_perform.
  CURLcode curl_result;
  /// The HTTP response code observed for the completed request.
  int64_t response_code = 0;
};

/**
 * @brief Read the HTTP response code from a curl handle.
 *
 * @param curl The curl handle associated with the completed request.
 * @return The HTTP response code reported by libcurl.
 */
int64_t Get_response_code(CURL *curl) {
  int64_t response_code{};
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  return response_code;
}

/**
 * @brief Sleep for the given number of seconds before retrying a request.
 *
 * @param seconds The retry delay in seconds.
 */
void Sleep_for_seconds(const uint8_t seconds) {
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

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

// Helper for access to the mutable curl hook set
Curl_api_hooks &Get_curl_api_hooks() {
  static Curl_api_hooks curl_api_hooks{};
  return curl_api_hooks;
}

// Helper to simulate curl_easy_init failure in tests
CURL *Fail_curl_easy_init() { return nullptr; }

// Helper for policy-aware error logging
void Log_error(const ERROR_LOG_POLICY policy, const std::string &message) {
  if (policy == ERROR_LOG_POLICY::LOG_AS_DEBUG) {
    LOG_DEBUG(message);
    return;
  }
  LOG_ERROR(message);
}

// Helper for CURL responses
size_t Curl_write_callback(void *contents, const size_t size,
                           const size_t nmemb, std::string *response) {
  const size_t real_size = size * nmemb;
  response->append(static_cast<char *>(contents), real_size);
  return real_size;
}

// Helper for default HTTP request headers
curl_slist *Default_headers(const std::string &bearer_token) {
  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "User-Agent: IQM QDMI C++ API");
  if (!bearer_token.empty()) {
    // Set the "Authorization" header with the bearer token
    headers =
        curl_slist_append(headers, ("Authorization: " + bearer_token).c_str());
  }
  return headers;
}

// Helper for HTTP response code handling and logging
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
      // Response is not JSON or invalid JSON, continue with plain text handling
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

/**
 * @brief Execute a configured request and retry on HTTP 429 responses.
 *
 * The supplied callback performs one request attempt and returns both the
 * libcurl result and the resulting HTTP response code.
 */
template <typename Perform_attempt, typename Sleep_function>
int Perform_request_with_retries(const std::string &url, std::string &response,
                                 const ERROR_LOG_POLICY error_log_policy,
                                 Perform_attempt perform_attempt,
                                 Sleep_function sleep_for_seconds) {
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

    LOG_DEBUG("Request to URL '" + url +
              "' hit HTTP 429 rate limiting; retrying in " +
              std::to_string(RATE_LIMIT_RETRY_DELAY_SECONDS) +
              " second(s) (attempt " + std::to_string(attempt + 1) + "/" +
              std::to_string(RATE_LIMIT_RETRY_COUNT) + ")");
    sleep_for_seconds(RATE_LIMIT_RETRY_DELAY_SECONDS);
  }

  return QDMI_ERROR_FATAL;
}

int Perform_get_request(const std::string &url, const std::string &bearer_token,
                        std::string &response,
                        const ERROR_LOG_POLICY error_log_policy) {
  LOG_INFO("Performing GET request to " + url);
  auto &curl_api_hooks = Get_curl_api_hooks();
  CURL *curl = curl_api_hooks.easy_init();
  if (curl == nullptr) {
    LOG_ERROR("curl_easy_init() failed");
    return QDMI_ERROR_FATAL;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  auto *headers = Default_headers(bearer_token);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  const auto ret = Perform_request_with_retries(
      url, response, error_log_policy,
      [&]() -> Request_attempt_result {
        const auto res = curl_api_hooks.easy_perform(curl);
        if (res != CURLE_OK) {
          return Request_attempt_result{res, 0};
        }
        return Request_attempt_result{CURLE_OK, Get_response_code(curl)};
      },
      Sleep_for_seconds);
  curl_easy_cleanup(curl); // NOLINT(misc-include-cleaner)
  curl_slist_free_all(headers);
  return ret;
}
} // namespace

int CurlHttpClient::get(const std::string &url, const std::string &bearer_token,
                        std::string &response) {
  return Perform_get_request(url, bearer_token, response,
                             ERROR_LOG_POLICY::LOG_AS_ERROR);
}

int CurlHttpClient::get_optional(const std::string &url,
                                 const std::string &bearer_token,
                                 std::string &response) {
  return Perform_get_request(url, bearer_token, response,
                             ERROR_LOG_POLICY::LOG_AS_DEBUG);
}

/**
 * @brief Perform an HTTP POST request using cURL.
 *
 * Sends an authenticated POST request, attaches a JSON content type header,
 * and appends an optional extra header when provided.
 *
 * @param url The target URL for the POST request.
 * @param bearer_token The bearer token for authentication.
 * @param response Reference to the response body buffer.
 * @param data The request payload to send.
 * @param extra_header An optional extra HTTP header.
 * @return The mapped QDMI status code for the request outcome.
 */
int CurlHttpClient::post(const std::string &url,
                         const std::string &bearer_token, std::string &response,
                         const std::string &data,
                         const std::string &extra_header) {
  LOG_INFO("Performing POST request to " + url);
  auto &curl_api_hooks = Get_curl_api_hooks();
  CURL *curl = curl_api_hooks.easy_init();
  if (curl == nullptr) {
    LOG_ERROR("curl_easy_init() failed");
    return QDMI_ERROR_FATAL;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  auto *headers = Default_headers(bearer_token);
  headers = curl_slist_append(headers, "Content-Type: application/json");
  if (!extra_header.empty()) {
    headers = curl_slist_append(headers, extra_header.c_str());
  }
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (!data.empty()) {
    LOG_DEBUG("POST data: " + data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
  } else {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
  }
  const auto ret = Perform_request_with_retries(
      url, response, ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() -> Request_attempt_result {
        const auto res = curl_api_hooks.easy_perform(curl);
        if (res != CURLE_OK) {
          return Request_attempt_result{res, 0};
        }
        return Request_attempt_result{CURLE_OK, Get_response_code(curl)};
      },
      Sleep_for_seconds);
  curl_easy_cleanup(curl); // NOLINT(misc-include-cleaner)
  curl_slist_free_all(headers);
  return ret;
}

namespace test_support {

// NOLINTNEXTLINE(misc-use-internal-linkage)
int Handle_response_code_for_testing(const int64_t response_code,
                                     const std::string &url,
                                     const std::string &response,
                                     const bool use_debug_logging) {
  return Handle_response_code(response_code, url, response,
                              use_debug_logging
                                  ? ERROR_LOG_POLICY::LOG_AS_DEBUG
                                  : ERROR_LOG_POLICY::LOG_AS_ERROR);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
void Set_curl_api_hooks_for_testing(CURL *(*easy_init)(),
                                    CURLcode (*easy_perform)(CURL *)) {
  auto &curl_api_hooks = Get_curl_api_hooks();
  curl_api_hooks.easy_init = easy_init != nullptr ? easy_init : curl_easy_init;
  curl_api_hooks.easy_perform =
      easy_perform != nullptr ? easy_perform : curl_easy_perform;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
void Enable_curl_easy_init_failure_for_testing() {
  Set_curl_api_hooks_for_testing(Fail_curl_easy_init, nullptr);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
Retry_test_result
Retry_response_codes_for_testing(const std::vector<int64_t> &response_codes,
                                 const std::string &url,
                                 const bool use_debug_logging) {
  size_t next_response_code_index = 0;
  size_t sleep_call_count = 0;
  std::string response;
  const auto status_code = Perform_request_with_retries(
      url, response,
      use_debug_logging ? ERROR_LOG_POLICY::LOG_AS_DEBUG
                        : ERROR_LOG_POLICY::LOG_AS_ERROR,
      [&]() -> Request_attempt_result {
        if (next_response_code_index >= response_codes.size()) {
          return Request_attempt_result{CURLE_OK, 0};
        }
        const auto response_code = response_codes[next_response_code_index];
        next_response_code_index++;
        return Request_attempt_result{CURLE_OK, response_code};
      },
      [&]([[maybe_unused]] const uint8_t seconds) { sleep_call_count++; });
  return Retry_test_result{status_code, sleep_call_count};
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
void Reset_curl_api_hooks_for_testing() {
  Set_curl_api_hooks_for_testing(nullptr, nullptr);
}

} // namespace test_support

} // namespace iqm
