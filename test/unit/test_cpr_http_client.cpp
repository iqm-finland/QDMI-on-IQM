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

#include "cpr_http_client.hpp"
#include "cpr_http_client_test_support.hpp"
#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>

namespace {

/**
 * @brief Captures logger output for assertions within a single test scope.
 */
class LoggerCapture {
public:
  /**
   * @brief Redirect logger output to an internal string stream.
   */
  LoggerCapture()
      : logger_(&iqm::Logger::get_instance()),
        original_level_(logger_->get_level()) {
    logger_->set_level(iqm::LOG_LEVEL::DEBUG);
    logger_->set_output(log_stream_);
  }

  LoggerCapture(const LoggerCapture &) = delete;
  LoggerCapture &operator=(const LoggerCapture &) = delete;
  LoggerCapture(LoggerCapture &&) = delete;
  LoggerCapture &operator=(LoggerCapture &&) = delete;

  /**
   * @brief Restore the original logger output stream and level.
   */
  ~LoggerCapture() {
    logger_->set_output(std::cerr);
    logger_->set_level(original_level_);
  }

  /**
   * @brief Return the captured log output.
   * @return The current contents of the capture stream.
   */
  [[nodiscard]] std::string str() const { return log_stream_.str(); }

private:
  /// Logger singleton used for temporary output redirection.
  iqm::Logger *logger_;
  /// Original log level restored when the guard goes out of scope.
  iqm::LOG_LEVEL original_level_;
  /// Buffer that captures log output for test assertions.
  std::stringstream log_stream_;
};

/**
 * @brief Restores CPR test hooks when leaving scope.
 */
class CprApiHookGuard {
public:
  /**
   * @brief Install a temporary CPR request failure hook.
   */
  CprApiHookGuard() {
    iqm::test_support::Enable_cpr_request_failure_for_testing();
  }

  CprApiHookGuard(const CprApiHookGuard &) = delete;
  CprApiHookGuard &operator=(const CprApiHookGuard &) = delete;
  CprApiHookGuard(CprApiHookGuard &&) = delete;
  CprApiHookGuard &operator=(CprApiHookGuard &&) = delete;

  /**
   * @brief Restore the default CPR hooks.
   */
  ~CprApiHookGuard() { iqm::test_support::Reset_cpr_api_hooks_for_testing(); }
};

TEST(CprHttpClientTest, SuccessMessagesAreLogged) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::test_support::Handle_response_code_for_testing(
      200, "https://example.test/jobs",
      R"({"messages":[{"message":"alpha"},{"message":7},{"ignored":true}]})",
      false);

  EXPECT_EQ(ret, QDMI_SUCCESS);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("Response contains 3 message(s):"), std::string::npos);
  EXPECT_NE(logs.find("  - alpha"), std::string::npos);
  EXPECT_NE(logs.find("Request successful (HTTP 200)"), std::string::npos);
}

TEST(CprHttpClientTest, InvalidJsonServerErrorFallsBackToRawResponse) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::test_support::Handle_response_code_for_testing(
      503, "https://example.test/jobs", "not-json", false);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("Response is not valid JSON"), std::string::npos);
  EXPECT_NE(logs.find("failed with HTTP 503 (Server Error)"),
            std::string::npos);
  EXPECT_NE(logs.find("Response: not-json"), std::string::npos);
}

TEST(CprHttpClientTest, RedirectResponseLogsAdditionalMessages) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::test_support::Handle_response_code_for_testing(
      302, "https://example.test/jobs",
      R"({"messages":[{"message":"follow redirect"}]})", false);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 302 (Unexpected Redirect)"),
            std::string::npos);
  EXPECT_NE(logs.find("Response contains 1 additional message(s):"),
            std::string::npos);
  EXPECT_NE(logs.find("  - follow redirect"), std::string::npos);
  EXPECT_NE(
      logs.find("Response: {\"messages\":[{\"message\":\"follow redirect\"}]}"),
      std::string::npos);
}

TEST(CprHttpClientTest, StructuredErrorsSuppressRawFallback) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::test_support::Handle_response_code_for_testing(
      600, "https://example.test/jobs",
      R"({"errors":[{"error_code":"E1","message":"boom"},{"message":"detail"},{"error_code":7},{"ignored":true}]})",
      false);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 600 (Unexpected Response Code)"),
            std::string::npos);
  EXPECT_NE(logs.find("Response contains 4 error(s):"), std::string::npos);
  EXPECT_NE(logs.find("  - [E1] boom"), std::string::npos);
  EXPECT_NE(logs.find("  - detail"), std::string::npos);
  EXPECT_EQ(logs.find("Response: {\"errors\":"), std::string::npos);
}

TEST(CprHttpClientTest, GetReturnsFatalWhenCprRequestFails) {
  const LoggerCapture logger_capture;
  const CprApiHookGuard cpr_api_hook_guard;
  iqm::CprHttpClient http_client;
  std::string response;

  EXPECT_EQ(http_client.get("https://example.test/jobs", "", response),
            QDMI_ERROR_FATAL);
  EXPECT_NE(logger_capture.str().find(
                "CPR request failed: Failed to connect to host"),
            std::string::npos);
}

TEST(CprHttpClientTest, PostReturnsFatalWhenCprRequestFails) {
  const LoggerCapture logger_capture;
  const CprApiHookGuard cpr_api_hook_guard;
  iqm::CprHttpClient http_client;
  std::string response;

  EXPECT_EQ(
      http_client.post("https://example.test/jobs", "", response, "{}", ""),
      QDMI_ERROR_FATAL);
  EXPECT_NE(logger_capture.str().find(
                "CPR request failed: Failed to connect to host"),
            std::string::npos);
}

TEST(CprHttpClientTest, RetriesHttp429UntilSuccess) {
  const LoggerCapture logger_capture;
  const auto result = iqm::test_support::Retry_response_codes_for_testing(
      {429, 200}, "https://example.test/jobs", false);

  EXPECT_EQ(result.status_code, QDMI_SUCCESS);
  EXPECT_EQ(result.sleep_call_count, 1U);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("hit HTTP 429 rate limiting; retrying in 2 second(s)"),
            std::string::npos);
  EXPECT_NE(logs.find("Request successful (HTTP 200)"), std::string::npos);
}

TEST(CprHttpClientTest, RetriesExhaustedForHttp429ReturnInvalidArgument) {
  const LoggerCapture logger_capture;
  const auto result = iqm::test_support::Retry_response_codes_for_testing(
      {429, 429, 429, 429, 429, 429}, "https://example.test/jobs", false);

  EXPECT_EQ(result.status_code, QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(result.sleep_call_count, 5U);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 429 (Client Error)"),
            std::string::npos);
}

} // namespace
