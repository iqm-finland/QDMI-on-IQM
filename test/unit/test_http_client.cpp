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

#include "http_client.hpp"
#include "http_stub.hpp"
#include "iqm_qdmi/constants.h"
#include "logging.hpp"

#include <cpr/cprtypes.h>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

cpr::Response Make_response(const int64_t status_code, std::string url,
                            std::string body) {
  cpr::Response response;
  response.status_code = status_code;
  response.url = cpr::Url{std::move(url)};
  response.text = std::move(body);
  return response;
}

TEST(HttpClientTest, SuccessMessagesAreLogged) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::http::Handle_response(
      Make_response(
          200, "https://example.test/jobs",
          R"({"messages":[{"message":"alpha"},{"message":7},{"ignored":true}]})"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_SUCCESS);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("Response contains 3 message(s):"), std::string::npos);
  EXPECT_NE(logs.find("  - alpha"), std::string::npos);
  EXPECT_NE(logs.find("Request successful (HTTP 200)"), std::string::npos);
}

TEST(HttpClientTest, InvalidJsonServerErrorFallsBackToRawResponse) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::http::Handle_response(
      Make_response(503, "https://example.test/jobs", "not-json"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("Response is not valid JSON"), std::string::npos);
  EXPECT_NE(logs.find("failed with HTTP 503 (Server Error)"),
            std::string::npos);
  EXPECT_NE(logs.find("Response: not-json"), std::string::npos);
}

TEST(HttpClientTest, RedirectResponseLogsAdditionalMessages) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::http::Handle_response(
      Make_response(302, "https://example.test/jobs",
                    R"({"messages":[{"message":"follow redirect"}]})"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

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

TEST(HttpClientTest, StructuredErrorsSuppressRawFallback) {
  const LoggerCapture logger_capture;

  const auto ret = iqm::http::Handle_response(
      Make_response(
          600, "https://example.test/jobs",
          R"({"errors":[{"error_code":"E1","message":"boom"},{"message":"detail"},{"error_code":7},{"ignored":true}]})"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 600 (Unexpected Response Code)"),
            std::string::npos);
  EXPECT_NE(logs.find("Response contains 4 error(s):"), std::string::npos);
  EXPECT_NE(logs.find("  - [E1] boom"), std::string::npos);
  EXPECT_NE(logs.find("  - detail"), std::string::npos);
  EXPECT_EQ(logs.find("Response: {\"errors\":"), std::string::npos);
}

TEST(HttpClientTest, GetReturnsFatalWhenRequestFails) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get_connection_error();
  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);

  EXPECT_EQ(iqm::http::Handle_response(response), QDMI_ERROR_FATAL);
  EXPECT_NE(
      logger_capture.str().find("Request failed: Failed to connect to host"),
      std::string::npos);
}

TEST(HttpClientTest, PostReturnsFatalWhenRequestFails) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_post_connection_error();
  const auto response =
      iqm::http::Post("https://example.test/jobs", std::nullopt, "{}");

  EXPECT_EQ(iqm::http::Handle_response(response), QDMI_ERROR_FATAL);
  EXPECT_NE(
      logger_capture.str().find("Request failed: Failed to connect to host"),
      std::string::npos);
}

TEST(HttpClientTest, BearerTokenIsPassedToHooks) {
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get(200).queue_post(200);
  const auto bearer_token = cpr::Bearer{"test-token"};

  const auto get_response =
      iqm::http::Get("https://example.test/jobs", bearer_token);
  const auto post_response =
      iqm::http::Post("https://example.test/jobs", bearer_token, "{}");

  EXPECT_EQ(iqm::http::Handle_response(get_response), QDMI_SUCCESS);
  EXPECT_EQ(iqm::http::Handle_response(post_response), QDMI_SUCCESS);

  ASSERT_EQ(http_stub.get_bearer_tokens().size(), 1U);
  ASSERT_TRUE(http_stub.get_bearer_tokens()[0].has_value());
  EXPECT_EQ(std::string{http_stub.get_bearer_tokens()[0]->GetToken()},
            "test-token");
  ASSERT_EQ(http_stub.post_bearer_tokens().size(), 1U);
  ASSERT_TRUE(http_stub.post_bearer_tokens()[0].has_value());
  EXPECT_EQ(std::string{http_stub.post_bearer_tokens()[0]->GetToken()},
            "test-token");
}

TEST(HttpClientTest, RetriesHttp429UsingRetryAfterUntilSuccess) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get(429, "", {{"Retry-After", "30"}}).queue_get(200);

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(response.status_code, 200);
  EXPECT_EQ(http_stub.sleep_call_count(), 1U);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("hit HTTP 429 rate limiting; retrying after 30 second(s) "
                      "from the Retry-After header"),
            std::string::npos);
  EXPECT_NE(logs.find("Request successful (HTTP 200)"), std::string::npos);
}

TEST(HttpClientTest, RetriesHttp429UsingCaseInsensitiveRetryAfterHeader) {
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get(429, "", {{"retry-after", "7"}}).queue_get(200);

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{7});
}

TEST(HttpClientTest,
     RetriesHttp429WithConservativeFallbackForMissingRetryAfter) {
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get(429).queue_get(200);

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
}

TEST(HttpClientTest,
     RetriesHttp429WithConservativeFallbackForMalformedRetryAfter) {
  iqm::test_support::HttpStub http_stub;
  http_stub.queue_get(429, "", {{"Retry-After", "soon"}}).queue_get(200);

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
}

TEST(HttpClientTest, RetriesExhaustedForHttp429ReturnsInvalidArgument) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  for (int i = 0; i < 11; ++i) {
    http_stub.queue_get(429, "", {{"Retry-After", "1"}});
  }

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(response.status_code, 429);
  EXPECT_EQ(http_stub.sleep_call_count(), 10U);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 429 (Client Error)"),
            std::string::npos);
}

} // namespace
