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

#include <chrono>
#include <cpr/body.h>
#include <cpr/connection_pool.h>
#include <cpr/cprtypes.h>
#include <cpr/response.h>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <string>
#include <string_view>
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
   * @param level Level the logger runs at while the guard is alive.
   */
  explicit LoggerCapture(const iqm::LOG_LEVEL level = iqm::LOG_LEVEL::DEBUG)
      : logger_(&iqm::Logger::get_instance()),
        original_level_(logger_->get_level()) {
    logger_->set_level(level);
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

/// Names the share of the quota below which requests hold back.
constexpr auto THRESHOLD_VARIABLE = "IQM_RATE_LIMIT_THRESHOLD_PERCENT";

/// Headers reporting the documented IQM Server API quota with @p remaining
/// units left in the current window.
cpr::Header Quota_headers(const std::string &remaining) {
  return {{"RateLimit-Limit", "2000"}, {"RateLimit-Remaining", remaining}};
}

/**
 * @brief Fixture for the wait that precedes a request when the quota runs low.
 *
 * Holds the session's quota and pins the threshold variable off the
 * environment, so a value the developer exports cannot change what a test
 * means.
 */
class RateLimitTest : public testing::Test {
protected:
  void SetUp() override {
    if (const char *existing = std::getenv(THRESHOLD_VARIABLE);
        existing != nullptr) {
      previous_threshold_ = existing;
    }
    set_threshold(nullptr);
  }

  void TearDown() override {
    set_threshold(previous_threshold_.has_value() ? previous_threshold_->c_str()
                                                  : nullptr);
  }

  /// Set the threshold to @p percent, or unset it to get the built-in default.
  static void set_threshold(const char *percent) {
#ifdef _WIN32
    static_cast<void>(
        _putenv_s(THRESHOLD_VARIABLE, percent == nullptr ? "" : percent));
#else
    static_cast<void>(percent == nullptr
                          ? unsetenv(THRESHOLD_VARIABLE)
                          : setenv(THRESHOLD_VARIABLE, percent, 1));
#endif
  }

  /// Issue @p count requests on this session, spending its quota.
  void get(const int count,
           const std::chrono::milliseconds timeout = std::chrono::hours{1}) {
    for (int request = 0; request < count; ++request) {
      static_cast<void>(iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool_, timeout,
                                       &budget_));
    }
  }

  iqm::test_support::HttpStub http_stub_;
  cpr::ConnectionPool connection_pool_;
  iqm::http::Rate_limit_budget budget_;

private:
  std::optional<std::string> previous_threshold_;
};

cpr::Response Make_response(const int64_t status_code, std::string url,
                            std::string body,
                            const std::string &content_type = "") {
  cpr::Response response;
  response.status_code = status_code;
  response.url = cpr::Url{std::move(url)};
  response.text = std::move(body);
  if (!content_type.empty()) {
    response.header["Content-Type"] = content_type;
  }
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

TEST(HttpClientTest, RawResponseBodyStaysOutOfErrorLevelLogs) {
  const LoggerCapture logger_capture{iqm::LOG_LEVEL::ERROR};
  constexpr auto body = R"({"access_token":"do-not-log-me"})";

  const auto ret = iqm::http::Handle_response(
      Make_response(500, "https://example.test/jobs", body, "application/json"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 500 (Server Error)"),
            std::string::npos);
  EXPECT_EQ(logs.find("do-not-log-me"), std::string::npos);

  // The shape of the body still reaches the default log level, so an opaque
  // upstream failure remains diagnosable without raising verbosity.
  EXPECT_NE(logs.find("Response carries no structured error: " +
                      std::to_string(std::string_view{body}.size()) +
                      " byte(s) of application/json"),
            std::string::npos);
}

TEST(HttpClientTest, UntypedResponseBodyIsReportedWithoutAContentType) {
  const LoggerCapture logger_capture{iqm::LOG_LEVEL::ERROR};

  const auto ret = iqm::http::Handle_response(
      Make_response(502, "https://example.test/jobs", "<html>gateway</html>"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);
  EXPECT_NE(logger_capture.str().find("20 byte(s) of an unnamed content type"),
            std::string::npos);
}

TEST(HttpClientTest, RawResponseBodyReachesDebugLevelLogs) {
  const LoggerCapture logger_capture{iqm::LOG_LEVEL::DEBUG};

  const auto ret = iqm::http::Handle_response(
      Make_response(500, "https://example.test/jobs", "opaque-upstream-detail"),
      iqm::http::ERROR_LOG_POLICY::LOG_AS_ERROR);

  EXPECT_EQ(ret, QDMI_ERROR_FATAL);
  EXPECT_NE(logger_capture.str().find("Response: opaque-upstream-detail"),
            std::string::npos);
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
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get_connection_error();
  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);

  EXPECT_EQ(iqm::http::Handle_response(response), QDMI_ERROR_FATAL);
  EXPECT_NE(
      logger_capture.str().find("Request failed: Failed to connect to host"),
      std::string::npos);
}

TEST(HttpClientTest, PostReturnsFatalWhenRequestFails) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_post_connection_error();
  const auto response = iqm::http::Post("https://example.test/jobs",
                                        std::nullopt, connection_pool, "{}");

  EXPECT_EQ(iqm::http::Handle_response(response), QDMI_ERROR_FATAL);
  EXPECT_NE(
      logger_capture.str().find("Request failed: Failed to connect to host"),
      std::string::npos);
}

TEST(HttpClientTest, BearerTokenIsPassedToHooks) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(200).queue_get(200).queue_post(200);
  const auto bearer_token = cpr::Bearer{"test-token"};

  const auto get_response = iqm::http::Get("https://example.test/jobs",
                                           bearer_token, connection_pool);
  const auto probe_get_response = iqm::http::Get(
      "https://example.test/capability", bearer_token, connection_pool);
  const auto post_response = iqm::http::Post(
      "https://example.test/jobs", bearer_token, connection_pool, "{}");

  EXPECT_EQ(iqm::http::Handle_response(get_response), QDMI_SUCCESS);
  EXPECT_EQ(iqm::http::Handle_response(probe_get_response), QDMI_SUCCESS);
  EXPECT_EQ(iqm::http::Handle_response(post_response), QDMI_SUCCESS);

  ASSERT_EQ(http_stub.get_bearer_tokens().size(), 2U);
  ASSERT_TRUE(http_stub.get_bearer_tokens()[0].has_value());
  EXPECT_EQ(std::string{http_stub.get_bearer_tokens()[0]->GetToken()},
            "test-token");
  ASSERT_EQ(http_stub.post_bearer_tokens().size(), 1U);
  ASSERT_TRUE(http_stub.post_bearer_tokens()[0].has_value());
  EXPECT_EQ(std::string{http_stub.post_bearer_tokens()[0]->GetToken()},
            "test-token");
  EXPECT_EQ(http_stub.get_connection_pools(),
            (std::vector<const cpr::ConnectionPool *>{&connection_pool,
                                                      &connection_pool}));
  EXPECT_EQ(http_stub.post_connection_pools(),
            (std::vector<const cpr::ConnectionPool *>{&connection_pool}));
}

TEST(HttpClientTest, ExplicitTimeoutIsPassedToHooks) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(200).queue_post(200);
  constexpr auto timeout = std::chrono::milliseconds{1'234};

  const auto get_response = iqm::http::Get(
      "https://example.test/jobs", std::nullopt, connection_pool, timeout);
  const auto post_response =
      iqm::http::Post("https://example.test/jobs", std::nullopt,
                      connection_pool, "{}", {}, timeout);

  EXPECT_EQ(iqm::http::Handle_response(get_response), QDMI_SUCCESS);
  EXPECT_EQ(iqm::http::Handle_response(post_response), QDMI_SUCCESS);
  EXPECT_EQ(http_stub.get_timeouts(),
            std::vector<std::chrono::milliseconds>{timeout});
  EXPECT_EQ(http_stub.post_timeouts(),
            std::vector<std::chrono::milliseconds>{timeout});
}

TEST(HttpClientTest, TimeoutIsClampedToTransportRepresentation) {
  constexpr auto timeout = std::chrono::milliseconds::max();
  constexpr auto clamped =
      iqm::http::internal::Clamp_timeout_for_transport<std::int32_t>(timeout);
  EXPECT_EQ(clamped.count(), std::numeric_limits<std::int32_t>::max());
  EXPECT_EQ(
      iqm::http::internal::Clamp_timeout_for_transport<std::int64_t>(timeout),
      timeout);
}

TEST(HttpClientTest, RetriesHttp429UsingRetryAfterUntilSuccess) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429, "", {{"Retry-After", "30"}}).queue_get(200);

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(response.status_code, 200);
  EXPECT_EQ(http_stub.sleep_call_count(), 1U);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
  EXPECT_EQ(http_stub.get_connection_pools(),
            (std::vector<const cpr::ConnectionPool *>{&connection_pool,
                                                      &connection_pool}));

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("hit HTTP 429 rate limiting; retrying after 30 second(s) "
                      "from the Retry-After header"),
            std::string::npos);
  EXPECT_NE(logs.find("Request successful (HTTP 200)"), std::string::npos);
}

TEST(HttpClientTest, RetriesHttp429UsingCaseInsensitiveRetryAfterHeader) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429, "", {{"retry-after", "7"}}).queue_get(200);

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{7});
}

TEST(HttpClientTest, RateLimitRetryDoesNotOutliveRequestTimeout) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429, "", {{"Retry-After", "30"}}).queue_get(200);

  const auto response =
      iqm::http::Get("https://example.test/jobs", std::nullopt, connection_pool,
                     std::chrono::milliseconds{1'000});
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(response.status_code, 429);
  EXPECT_EQ(http_stub.sleep_call_count(), 0U);
  EXPECT_EQ(http_stub.get_timeouts(), std::vector<std::chrono::milliseconds>{
                                          std::chrono::milliseconds{1'000}});
  EXPECT_NE(logger_capture.str().find(
                "Retry-After exceeds the remaining request timeout"),
            std::string::npos);
}

TEST(HttpClientTest, MaximumRequestTimeoutDoesNotOverflowRetryBudget) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429, "", {{"Retry-After", "30"}}).queue_get(200);
  constexpr auto timeout = std::chrono::milliseconds::max();

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool, timeout);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  ASSERT_EQ(http_stub.get_timeouts().size(), 2U);
  EXPECT_EQ(http_stub.get_timeouts().front(), timeout);
  EXPECT_GT(http_stub.get_timeouts().back(), std::chrono::milliseconds::zero());
  EXPECT_LE(http_stub.get_timeouts().back(), timeout);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
}

TEST(HttpClientTest,
     RetriesHttp429WithConservativeFallbackForMissingRetryAfter) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429).queue_get(200);

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
}

TEST(HttpClientTest,
     RetriesHttp429WithConservativeFallbackForMalformedRetryAfter) {
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  http_stub.queue_get(429, "", {{"Retry-After", "soon"}}).queue_get(200);

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_SUCCESS);
  EXPECT_EQ(http_stub.sleep_durations(), std::vector<int>{30});
}

TEST(HttpClientTest, RetriesExhaustedForHttp429ReturnsInvalidArgument) {
  const LoggerCapture logger_capture;
  iqm::test_support::HttpStub http_stub;
  const cpr::ConnectionPool connection_pool;
  for (int i = 0; i < 11; ++i) {
    http_stub.queue_get(429, "", {{"Retry-After", "1"}});
  }

  const auto response = iqm::http::Get("https://example.test/jobs",
                                       std::nullopt, connection_pool);
  const auto status = iqm::http::Handle_response(response);

  EXPECT_EQ(status, QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(response.status_code, 429);
  EXPECT_EQ(http_stub.sleep_call_count(), 10U);

  const auto logs = logger_capture.str();
  EXPECT_NE(logs.find("failed with HTTP 429 (Client Error)"),
            std::string::npos);
}

TEST_F(RateLimitTest, WaitsForTheWindowWhenTheQuotaRunsLow) {
  const LoggerCapture logger_capture;
  http_stub_.queue_get(200, "", Quota_headers("50")).queue_get(200);

  get(1);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
  get(1);

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_durations(), std::vector<int>{10});
  // The wait comes out of the request's own timeout.
  EXPECT_EQ(http_stub_.get_timeouts().back(),
            std::chrono::hours{1} - std::chrono::seconds{10});
  EXPECT_NE(logger_capture.str().find("is holding back for 10 second(s) to let "
                                      "the rate-limit window replenish"),
            std::string::npos);
}

TEST_F(RateLimitTest, DoesNotWaitWhileTheQuotaIsHealthy) {
  // 200 units is exactly the default threshold of ten percent, which is not
  // yet low enough to hold anything back.
  http_stub_.queue_get(200, "", Quota_headers("200")).queue_get(200);

  get(2);

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
}

TEST_F(RateLimitTest, DoesNotWaitWithoutUsableRateLimitHeaders) {
  http_stub_.queue_get(200)
      .queue_get(200, "", {{"RateLimit-Remaining", "50"}})
      .queue_get(
          200, "",
          {{"RateLimit-Limit", "2000"}, {"RateLimit-Remaining", "none left"}})
      .queue_get(200);

  get(4);

  EXPECT_EQ(http_stub_.get_urls().size(), 4U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
}

TEST_F(RateLimitTest, IgnoresRateLimitHeadersOnAFailedResponse) {
  http_stub_.queue_get(500, "", Quota_headers("0")).queue_get(200);

  get(2);

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
}

TEST_F(RateLimitTest, DoesNotWaitOnAQuotaReadingOlderThanTheWindow) {
  http_stub_.queue_get(200, "", Quota_headers("50")).queue_get(200);

  get(1);
  http_stub_.advance(std::chrono::seconds{10});
  get(1);

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
}

TEST_F(RateLimitTest, DoesNotWaitLongerThanTheRequestTimeout) {
  const LoggerCapture logger_capture;
  http_stub_.queue_get(200, "", Quota_headers("50")).queue_get(200);

  get(1);
  get(1, std::chrono::seconds{5});

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
  EXPECT_NE(logger_capture.str().find("exceeds the request timeout"),
            std::string::npos);
}

TEST_F(RateLimitTest, AZeroThresholdTurnsTheWaitingOff) {
  set_threshold("0");
  // An overdrawn quota is the extreme the knob still has to let through.
  http_stub_.queue_get(200, "", Quota_headers("-5")).queue_get(200);

  get(2);

  EXPECT_EQ(http_stub_.get_urls().size(), 2U);
  EXPECT_EQ(http_stub_.sleep_call_count(), 0U);
}

TEST_F(RateLimitTest, AConfiguredThresholdMovesThePoint) {
  set_threshold("50");
  // Healthy at the default threshold of ten percent, low at fifty.
  http_stub_.queue_get(200, "", Quota_headers("900")).queue_get(200);

  get(2);

  EXPECT_EQ(http_stub_.sleep_durations(), std::vector<int>{10});
}

TEST_F(RateLimitTest, AMalformedThresholdFallsBackToTheDefault) {
  const LoggerCapture logger_capture;
  set_threshold("101");
  http_stub_.queue_get(200, "", Quota_headers("50")).queue_get(200);

  get(2);

  EXPECT_EQ(http_stub_.sleep_durations(), std::vector<int>{10});
  EXPECT_NE(logger_capture.str().find(
                "must be a whole percentage between 0 and 100; using 10"),
            std::string::npos);
}

} // namespace
