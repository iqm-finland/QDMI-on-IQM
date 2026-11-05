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

#include "iqm_qdmi/constants.h"
#include "iqm_qdmi/iteration.h"

#include "../iteration/iteration_management_smoke.hpp"
#include "http_client.hpp"
#include "http_stub.hpp"
#include "iqm_auth.hpp"

#include "cpr/connection_pool.h"
#include "cpr/response.h"

#include <array>
#include <chrono>
#include <cpr/error.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
// POSIX environment functions are not part of the C++ <cstdlib> contract.
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#endif
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace {
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;
constexpr const char *ID = "12345678-1234-1234-abcd-123456789abc";
std::string Reply(const std::string &state = "waiting") {
  return nlohmann::json{{"id", ID}, {"status", state}}.dump();
}
class IterationManagement : public ::testing::Test {
protected:
  iqm::test_support::HttpStub http_;
  IQM_QDMI_Iteration_Client client_ = nullptr;
  IQM_QDMI_Iteration_Result result_{};
  void SetUp() override {
    ASSERT_EQ(IQM_QDMI_iteration_client_alloc("https://example.test",
                                              "test-token", nullptr, 10000,
                                              &client_),
              QDMI_SUCCESS);
    ASSERT_NE(client_, nullptr);
    EXPECT_TRUE(http_.get_urls().empty());
    EXPECT_TRUE(http_.post_urls().empty());
  }
  void TearDown() override { IQM_QDMI_iteration_client_free(client_); }
  nlohmann::json diagnostics() {
    return nlohmann::json::parse(
        IQM_QDMI_iteration_client_diagnostics(client_));
  }
};

TEST_F(IterationManagement, CreateGetEndWithoutSessionAndWithAliasedId) {
  http_.queue_post(200, Reply());
  ASSERT_EQ(IQM_QDMI_iteration_create(client_, "emerald:mock", 60, 0, 30000,
                                      &result_),
            QDMI_SUCCESS);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_STREQ(result_.state, "waiting");
  EXPECT_EQ(result_.http_status, 200);
  EXPECT_EQ(http_.post_urls().back(), "https://example.test/api/v1/iterations");
  EXPECT_EQ(
      nlohmann::json::parse(http_.post_bodies().back()),
      (nlohmann::json{{"qc", "emerald:mock"}, {"max_duration_seconds", 60}}));
  http_.queue_get(200, Reply("active"));
  ASSERT_EQ(
      IQM_QDMI_iteration_get(client_, result_.iteration_id, 30000, &result_),
      QDMI_SUCCESS);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_EQ(http_.get_urls().back(),
            std::string("https://example.test/api/v1/iterations/") + ID);
  http_.queue_post(202, Reply("active"));
  ASSERT_EQ(
      IQM_QDMI_iteration_end(client_, result_.iteration_id, 30000, &result_),
      QDMI_SUCCESS);
  EXPECT_EQ(result_.http_status, 202);
  EXPECT_EQ(http_.post_urls().size(), 2);
  EXPECT_EQ(http_.get_urls().size(), 1);
  EXPECT_EQ(http_.post_urls().back(),
            std::string("https://example.test/api/v1/iterations/") + ID +
                "/end");
  http_.queue_get(200, Reply("active")).queue_get(200, Reply("completed"));
  ASSERT_EQ(
      IQM_QDMI_iteration_get(client_, result_.iteration_id, 30000, &result_),
      QDMI_SUCCESS);
  ASSERT_EQ(
      IQM_QDMI_iteration_get(client_, result_.iteration_id, 30000, &result_),
      QDMI_SUCCESS);
  EXPECT_STREQ(result_.state, "completed");
  EXPECT_EQ(http_.post_urls().size(), 2);
  EXPECT_EQ(http_.post_timeouts().front(), milliseconds{10000});
}

TEST_F(IterationManagement, ValidatesEveryArgumentBeforeNetworkActivity) {
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 60, 30000, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
  for (const auto max : {0U, 2701U}) {
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", max, 0, 30000, &result_),
              QDMI_ERROR_INVALIDARGUMENT);
  }
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 61, 30000, &result_),
            QDMI_ERROR_INVALIDARGUMENT);
  for (const auto *qc : {static_cast<const char *>(nullptr), ""}) {
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, qc, 60, 0, 30000, &result_),
              QDMI_ERROR_INVALIDARGUMENT);
  }
  for (const auto timeout :
       {uint64_t{0}, std::numeric_limits<uint64_t>::max()}) {
    EXPECT_EQ(
        IQM_QDMI_iteration_create(client_, "qc", 60, 0, timeout, &result_),
        QDMI_ERROR_INVALIDARGUMENT);
    std::strcpy(result_.iteration_id, ID);
    EXPECT_EQ(IQM_QDMI_iteration_get(client_, result_.iteration_id, timeout,
                                     &result_),
              QDMI_ERROR_INVALIDARGUMENT);
    EXPECT_STREQ(result_.iteration_id, ID);
    EXPECT_EQ(IQM_QDMI_iteration_end(client_, result_.iteration_id, timeout,
                                     &result_),
              QDMI_ERROR_INVALIDARGUMENT);
    EXPECT_STREQ(result_.iteration_id, ID);
  }
  for (const auto *id : {
           static_cast<const char *>(nullptr),
           "",
           "not-a-uuid",
           "12345678/1234-1234-abcd-123456789abc",
       }) {
    EXPECT_EQ(IQM_QDMI_iteration_get(client_, id, 30000, &result_),
              QDMI_ERROR_INVALIDARGUMENT);
    EXPECT_EQ(IQM_QDMI_iteration_end(client_, id, 30000, &result_),
              QDMI_ERROR_INVALIDARGUMENT);
  }
  EXPECT_EQ(IQM_QDMI_iteration_get(client_, ID, 30000, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_iteration_end(client_, ID, 30000, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_iteration_create(nullptr, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_TRUE(http_.post_urls().empty());
  EXPECT_TRUE(http_.get_urls().empty());
}

TEST_F(IterationManagement, AcceptsDurationBoundariesAndExplicitIdle) {
  for (const auto duration : {1U, 2700U}) {
    http_.queue_post(200, Reply());
    ASSERT_EQ(IQM_QDMI_iteration_create(client_, "qc", duration, duration,
                                        30000, &result_),
              QDMI_SUCCESS);
    EXPECT_EQ(nlohmann::json::parse(
                  http_.post_bodies().back())["idle_timeout_seconds"],
              duration);
  }
}

TEST_F(IterationManagement, RetainsConfirmedIdOnStateOrDiagnosticsFailure) {
  for (const auto &state : {
           nlohmann::json("future-state"),
           nlohmann::json(42),
           nlohmann::json(nullptr),
       }) {
    const auto body = nlohmann::json{
        {"id", "12345678-1234-1234-ABCD-123456789ABC"},
        {"status", state},
    };
    http_.queue_post(200, body.dump());
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
              QDMI_ERROR_FATAL);
    EXPECT_STREQ(result_.iteration_id, ID);
    EXPECT_STREQ(result_.state, "");
    EXPECT_EQ(result_.http_status, 200);
  }
  http_.queue_post(200,
                   nlohmann::json{
                       {"id", ID},
                       {"status", "waiting"},
                       {"request_id", 42},
                       {"future", "ignored"},
                   }
                       .dump());
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_SUCCESS);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_EQ(http_.post_urls().size(), 4);
}

TEST_F(IterationManagement, DoesNotRetryUncertainCreation) {
  for (const auto *body : {"{", "{}", "{\"id\":42}", R"({"id":"invalid"})"}) {
    http_.queue_post(200, body);
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
              QDMI_ERROR_FATAL);
    EXPECT_STREQ(result_.iteration_id, "");
    EXPECT_TRUE(diagnostics()["creation_uncertain"].get<bool>());
  }
  http_.queue_post_connection_error();
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(result_.http_status, 0);
  EXPECT_TRUE(diagnostics()["creation_uncertain"].get<bool>());
  http_.queue_post(500, "{}");
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_TRUE(diagnostics()["creation_uncertain"].get<bool>());
  EXPECT_EQ(http_.post_urls().size(), 6);
}

TEST_F(IterationManagement, PreservesInputIdAcrossErrorsAndRejectsMismatches) {
  for (const auto *body : {
           "{}",
           "{",
           "{\"id\":\"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa\","
           "\"status\":\"active\"}",
       }) {
    std::strcpy(result_.iteration_id, ID);
    http_.queue_get(200, body);
    EXPECT_EQ(
        IQM_QDMI_iteration_get(client_, result_.iteration_id, 30000, &result_),
        QDMI_ERROR_FATAL);
    EXPECT_STREQ(result_.iteration_id, ID);
  }
}

TEST_F(IterationManagement, MapsHttpErrorsAndSelectsDiagnostics) {
  for (const auto [http, expected] : {
           std::pair{400, QDMI_ERROR_INVALIDARGUMENT},
           {401, QDMI_ERROR_PERMISSIONDENIED},
           {403, QDMI_ERROR_PERMISSIONDENIED},
           {404, QDMI_ERROR_NOTFOUND},
           {429, QDMI_ERROR_INVALIDARGUMENT},
           {500, QDMI_ERROR_FATAL},
       }) {
    http_.queue_post(
        http,
        R"({"error_code":"server_reason","message":"reason","request_id":"trace","credentials":"excluded"})",
        {{"Retry-After", "30"}, {"private-header", "excluded"}});
    EXPECT_EQ(IQM_QDMI_iteration_end(client_, ID, 30000, &result_), expected);
    EXPECT_STREQ(result_.iteration_id, ID);
    EXPECT_EQ(result_.http_status, http);
    EXPECT_EQ(diagnostics()["error_code"], "server_reason");
    EXPECT_EQ(diagnostics()["message"], "reason");
    EXPECT_EQ(diagnostics()["request_id"], "trace");
    EXPECT_EQ(diagnostics()["http_status"], http);
    EXPECT_FALSE(diagnostics().contains("credentials"));
    EXPECT_FALSE(diagnostics().contains("private-header"));
  }
}

TEST_F(IterationManagement, RetriesOnlyExplicitRefusalWithinTotalDeadline) {
  http_.queue_post(429, "{}", {{"Retry-After", "30"}});
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_FALSE(diagnostics().contains("creation_uncertain"));
  EXPECT_EQ(http_.sleep_call_count(), 0);
  http_.queue_post(429, "{}", {{"Retry-After", "30"}}).queue_post(200, Reply());
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 90000, &result_),
            QDMI_SUCCESS);
  EXPECT_EQ(http_.post_urls().size(), 3);
  EXPECT_EQ(http_.sleep_durations(), std::vector<int>{30});
  for (const auto timeout : http_.post_timeouts()) {
    EXPECT_EQ(timeout, milliseconds{10000});
  }
}

TEST_F(IterationManagement, SupportsAllKnownStatesAndIdempotentEnd) {
  for (const auto *state :
       {"waiting", "active", "completed", "cancelled", "failed"}) {
    http_.queue_get(200, Reply(state));
    EXPECT_EQ(IQM_QDMI_iteration_get(client_, ID, 30000, &result_),
              QDMI_SUCCESS);
    EXPECT_STREQ(result_.state, state);
  }
  for (int i = 0; i < 2; ++i) {
    http_.queue_post(200, Reply("cancelled"));
    EXPECT_EQ(IQM_QDMI_iteration_end(client_, ID, 30000, &result_),
              QDMI_SUCCESS);
  }
  EXPECT_EQ(http_.post_urls().size(), 2);
}

TEST_F(IterationManagement, NullClientAndAllocationFailuresAreContained) {
  IQM_QDMI_iteration_client_free(nullptr);
  EXPECT_TRUE(
      nlohmann::json::parse(IQM_QDMI_iteration_client_diagnostics(nullptr))
          .is_object());
  EXPECT_EQ(
      IQM_QDMI_iteration_client_alloc(nullptr, nullptr, nullptr, 1, nullptr),
      QDMI_ERROR_INVALIDARGUMENT);
  auto *invalid = client_;
  EXPECT_EQ(
      IQM_QDMI_iteration_client_alloc("url", nullptr, nullptr, 0, &invalid),
      QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(invalid, nullptr);
  EXPECT_EQ(
      IQM_QDMI_iteration_client_alloc("url", "token", "file", 100, &invalid),
      QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(invalid, nullptr);
}

TEST_F(IterationManagement, ContainsBoundaryExceptionsAndPreservesConfirmedId) {
  auto &hook = iqm::http::internal::Get_hooks().post;
  for (const auto &[exception, expected] : std::array{
           std::pair{std::make_exception_ptr(std::bad_alloc{}),
                     QDMI_ERROR_OUTOFMEM},
           std::pair{std::make_exception_ptr(
                         iqm::ClientAuthenticationError{"secret"}),
                     QDMI_ERROR_PERMISSIONDENIED},
           std::pair{std::make_exception_ptr(42), QDMI_ERROR_FATAL},
       }) {
    hook = [exception](const auto &...) -> cpr::Response {
      std::rethrow_exception(exception);
    };
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
              expected);
    EXPECT_TRUE(diagnostics().is_object());
    EXPECT_EQ(std::string(IQM_QDMI_iteration_client_diagnostics(client_))
                  .find("secret"),
              std::string::npos);
  }
}

TEST_F(IterationManagement, RedirectsAreNotFollowedAndCredentialsAreRedacted) {
  auto &hook = iqm::http::internal::Get_hooks().post;
  const auto stub = hook;
  hook = [&](const auto &url, const auto &token, const auto &pool,
             const auto &headers, const auto body, const auto timeout,
             const bool follow) {
    EXPECT_FALSE(follow);
    return stub(url, token, pool, headers, body, timeout, follow);
  };
  for (const auto code : {301, 302, 307, 308}) {
    http_.queue_post(code, "{}", {{"Location", "https://other.test"}});
    EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
              QDMI_ERROR_FATAL);
  }
  EXPECT_EQ(http_.post_urls().size(), 4);
  http_.queue_post(403,
                   R"({"message":"test-token","request_id":"test-token"})");
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_PERMISSIONDENIED);
  EXPECT_EQ(diagnostics()["message"], "[redacted]");
  EXPECT_EQ(diagnostics()["request_id"], "[redacted]");
}

TEST_F(IterationManagement, SeparateClientsOwnCredentialsPoolsAndQuota) {
  IQM_QDMI_Iteration_Client other = nullptr;
  ASSERT_EQ(IQM_QDMI_iteration_client_alloc(
                "https://second.test/", "second-token", nullptr, 30000, &other),
            QDMI_SUCCESS);
  http_
      .queue_get(200, Reply(),
                 {{"RateLimit-Limit", "3000"}, {"RateLimit-Remaining", "0"}})
      .queue_get(200, Reply());
  ASSERT_EQ(IQM_QDMI_iteration_get(client_, ID, 30000, &result_), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_iteration_get(other, ID, 30000, &result_), QDMI_SUCCESS);
  EXPECT_NE(http_.get_connection_pools()[0], http_.get_connection_pools()[1]);
  EXPECT_STREQ(http_.get_bearer_tokens()[0]->GetToken(), "test-token");
  EXPECT_STREQ(http_.get_bearer_tokens()[1]->GetToken(), "second-token");
  EXPECT_EQ(http_.sleep_call_count(), 0);
  IQM_QDMI_iteration_client_free(other);
  EXPECT_TRUE(http_.post_urls().empty());
}

TEST_F(IterationManagement, CapsAttemptsByRemainingBudgetAndMapsLocalExpiry) {
  http_
      .queue_get(200, Reply(),
                 {{"RateLimit-Limit", "3000"}, {"RateLimit-Remaining", "0"}})
      .queue_get(200, Reply());
  ASSERT_EQ(IQM_QDMI_iteration_get(client_, ID, 30000, &result_), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_iteration_get(client_, ID, 13000, &result_), QDMI_SUCCESS);
  EXPECT_EQ(http_.sleep_durations(), std::vector<int>{10});
  EXPECT_EQ(http_.get_timeouts().back(), milliseconds{3000});
  auto &hook = iqm::http::internal::Get_hooks().get;
  hook = [&](const auto &...) {
    http_.advance(seconds{10});
    cpr::Response response;
    response.error.code = cpr::ErrorCode::OPERATION_TIMEDOUT;
    response.error.message = "timeout";
    return response;
  };
  EXPECT_EQ(IQM_QDMI_iteration_get(client_, ID, 10, &result_),
            QDMI_ERROR_TIMEOUT);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_EQ(result_.http_status, 0);
}

TEST(HttpDeadline, SubMillisecondRemainderDoesNotStartRequest) {
  // Hooks must record unexpected requests through their captured mutable this.
  // NOLINTNEXTLINE(misc-const-correctness)
  iqm::test_support::HttpStub http;
  const cpr::ConnectionPool pool;
  const auto deadline =
      iqm::http::internal::Get_hooks().now() + microseconds{500};
  const auto response = iqm::http::Get("https://example.test", std::nullopt,
                                       pool, seconds{10}, deadline, nullptr);
  EXPECT_EQ(response.error.code, cpr::ErrorCode::OPERATION_TIMEDOUT);
  EXPECT_TRUE(http.get_urls().empty());
}

class IterationSmoke : public ::testing::Test {
protected:
  iqm::test_support::HttpStub http_;
  std::filesystem::path scratch_;
  static constexpr std::array VARIABLES{
      "IQM_SERVER_URL",
      "IQM_TOKEN",
      "IQM_TOKENS_FILE",
      "IQM_ITERATION_SCRATCH_FILE",
  };
  std::array<std::optional<std::string>, VARIABLES.size()> previous_;
  static void set(const char *key, const char *value) {
#ifdef _WIN32
    ASSERT_EQ(_putenv_s(key, value == nullptr ? "" : value), 0);
#else
    ASSERT_EQ(value == nullptr ? unsetenv(key) : setenv(key, value, 1), 0);
#endif
  }
  void SetUp() override {
    for (size_t i = 0; i < VARIABLES.size(); ++i) {
      if (const auto *value = std::getenv(VARIABLES[i]); value != nullptr) {
        previous_[i] = value;
      }
      set(VARIABLES[i], nullptr);
    }
    scratch_ =
        std::filesystem::temp_directory_path() /
        (std::string("iqm-iteration-") +
         ::testing::UnitTest::GetInstance()->current_test_info()->name() +
         ".txt");
    std::filesystem::remove(scratch_);
    set("IQM_SERVER_URL", "https://example.test");
    set("IQM_TOKEN", "test-token");
    set("IQM_ITERATION_SCRATCH_FILE", scratch_.string().c_str());
  }
  void TearDown() override {
    for (size_t i = 0; i < VARIABLES.size(); ++i) {
      set(VARIABLES[i], previous_[i] ? previous_[i]->c_str() : nullptr);
    }
    std::filesystem::remove(scratch_);
  }
};
TEST_F(IterationSmoke, LifecycleRemovesScratchOnlyAfterTerminalConfirmation) {
  http_.queue_post(200, Reply()).queue_post(202, Reply("active"));
  http_.queue_get(200, Reply("active"))
      .queue_get(200, Reply("active"))
      .queue_get(200, Reply("completed"));
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(false), 0);
  EXPECT_EQ(std::filesystem::file_size(scratch_), 0);
  EXPECT_EQ(http_.post_urls().size(), 2);
  EXPECT_EQ(http_.get_urls().size(), 3);
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 0);
  EXPECT_EQ(http_.post_urls().size(), 2);
}
TEST_F(IterationSmoke, CleansConfirmedIdAfterCreateDecodingFailure) {
  http_.queue_post(200, Reply("unknown")).queue_post(200, Reply("cancelled"));
  http_.queue_get(200, Reply());
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(false), 1);
  EXPECT_EQ(std::filesystem::file_size(scratch_), 0);
  EXPECT_EQ(http_.post_urls().size(), 2);
}
TEST_F(IterationSmoke, ReconcilesKnownIdsAndRejectsInvalidScratchInput) {
  {
    std::ofstream file{scratch_};
    file << ID << "\n../../other\n";
  }
  http_.queue_get(200, Reply("failed"));
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
  EXPECT_EQ(http_.get_urls().size(), 1);
  EXPECT_TRUE(http_.post_urls().empty());
  std::ifstream file{scratch_};
  std::string retained;
  std::getline(file, retained);
  EXPECT_EQ(retained, "../../other");
}

TEST_F(IterationManagement, NullClientGetAndEndRetainAliasedId) {
  for (const auto function : {IQM_QDMI_iteration_get, IQM_QDMI_iteration_end}) {
    std::strcpy(result_.iteration_id, ID);
    std::strcpy(result_.state, "active");
    EXPECT_EQ(function(nullptr, result_.iteration_id, 30000, &result_),
              QDMI_ERROR_INVALIDARGUMENT);
    EXPECT_STREQ(result_.iteration_id, ID);
    EXPECT_STREQ(result_.state, "");
  }
}
TEST_F(IterationManagement, UnknownStateNeverExposesCredentials) {
  http_.queue_post(200, Reply("future-test-token-state"));
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_EQ(std::string(IQM_QDMI_iteration_client_diagnostics(client_))
                .find("test-token"),
            std::string::npos);
}

TEST_F(IterationManagement, ReusedClientPerformanceProbe) {
  for (const auto count : {10, 1000}) {
    for (const bool reuse : {true, false}) {
      const auto start_requests = http_.get_urls().size();
      for (int i = 0; i < count; ++i) {
        http_.queue_get(200, Reply("active"));
      }
      const auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < count; ++i) {
        IQM_QDMI_Iteration_Client current = client_;
        if (!reuse) {
          ASSERT_EQ(IQM_QDMI_iteration_client_alloc("https://example.test",
                                                    "test-token", nullptr,
                                                    10000, &current),
                    QDMI_SUCCESS);
        }
        ASSERT_EQ(IQM_QDMI_iteration_get(current, ID, 30000, &result_),
                  QDMI_SUCCESS);
        ASSERT_STREQ(result_.iteration_id, ID);
        ASSERT_STREQ(result_.state, "active");
        if (!reuse) {
          IQM_QDMI_iteration_client_free(current);
        }
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      EXPECT_EQ(http_.get_urls().size() - start_requests,
                static_cast<size_t>(count));
      if (reuse) {
        for (size_t i = start_requests; i < http_.get_connection_pools().size();
             ++i) {
          EXPECT_EQ(http_.get_connection_pools()[i],
                    http_.get_connection_pools()[start_requests]);
        }
      }
      std::cout << "PERF reads=" << count << " reuse=" << reuse
                << " requests=" << count << " elapsed_us=" << elapsed << '\n';
    }
  }
}

TEST_F(IterationManagement,
       RejectsUnexpectedSuccessCodesAndRetainsConfirmedId) {
  http_.queue_post(201, Reply());
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_STREQ(result_.iteration_id, ID);
  EXPECT_EQ(result_.http_status, 201);
  http_.queue_get(204);
  EXPECT_EQ(IQM_QDMI_iteration_get(client_, ID, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_STREQ(result_.iteration_id, ID);
  http_.queue_post(201, Reply("completed"));
  EXPECT_EQ(IQM_QDMI_iteration_end(client_, ID, 30000, &result_),
            QDMI_ERROR_FATAL);
  EXPECT_STREQ(result_.iteration_id, ID);
}

TEST_F(IterationManagement, OversizedRetryAfterCannotBecomeNegativeSleep) {
  http_.queue_post(429, "{}", {{"Retry-After", "9223372036854775807"}});
  EXPECT_EQ(IQM_QDMI_iteration_create(client_, "qc", 60, 0, 10000, &result_),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(http_.post_urls().size(), 1);
  EXPECT_EQ(http_.sleep_call_count(), 0);
}

TEST_F(IterationSmoke, UnreadableScratchCannotReportSuccessfulReconciliation) {
  std::filesystem::create_directory(scratch_);
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
  EXPECT_TRUE(http_.get_urls().empty());
  EXPECT_TRUE(http_.post_urls().empty());
}

TEST_F(IterationSmoke, MissingScratchNeedsNoReconciliation) {
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 0);
  EXPECT_TRUE(http_.get_urls().empty());
  EXPECT_TRUE(http_.post_urls().empty());
}

TEST_F(IterationSmoke, FailedScratchWritePreservesOtherUnconfirmedIds) {
  constexpr const char *other = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  const std::string original = std::string(ID) + "\n" + other + "\n";
  {
    std::ofstream file{scratch_};
    file << original;
  }
  const auto blocked = std::filesystem::path{scratch_.string() + ".tmp"};
  std::filesystem::create_directory(blocked);
  http_.queue_get(200, Reply("completed"))
      .queue_get(200,
                 nlohmann::json{{"id", other}, {"status", "completed"}}.dump());
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
  std::ifstream file{scratch_};
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>{file}, {}), original);
  std::filesystem::remove(blocked);
}

#ifdef _WIN32
TEST_F(IterationSmoke, FailedAtomicReplacementPreservesOtherUnconfirmedIds) {
  constexpr const char *other = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  const std::string original = std::string(ID) + "\n" + other + "\n";
  {
    std::ofstream file{scratch_};
    file << original;
  }
  auto *const locked = CreateFileW(
      scratch_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(locked, INVALID_HANDLE_VALUE);
  http_.queue_get(200, Reply("completed"))
      .queue_get(200,
                 nlohmann::json{{"id", other}, {"status", "completed"}}.dump());
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
  EXPECT_NE(CloseHandle(locked), 0);
  std::ifstream file{scratch_};
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>{file}, {}), original);
}
#endif

TEST_F(IterationSmoke, PermanentAccessFailureRetainsLedgerWithoutEnding) {
  {
    std::ofstream file{scratch_};
    file << ID << '\n';
  }
  for (const auto code : {401, 403, 404}) {
    const auto requests = http_.get_urls().size();
    http_.queue_get(code, R"({"error_code":"forbidden"})");
    EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
    EXPECT_EQ(http_.get_urls().size(), requests + 1);
    EXPECT_TRUE(http_.post_urls().empty());
    EXPECT_GT(std::filesystem::file_size(scratch_), 0);
  }
}

TEST_F(IterationSmoke, AcceptsCanonicalCrLfRecoveryIds) {
  {
    std::ofstream file{scratch_, std::ios::binary};
    file << ID << "\r\n";
  }
  http_.queue_get(200, Reply("completed"));
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 0);
  EXPECT_EQ(http_.get_urls().size(), 1);
  EXPECT_EQ(std::filesystem::file_size(scratch_), 0);
}
TEST_F(IterationSmoke, SuccessfulAtomicReplacementRetainsOnlyUnconfirmedIds) {
  constexpr const char *other = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  {
    std::ofstream file{scratch_};
    file << ID << '\n' << other << '\n';
  }
  http_.queue_get(200, Reply("completed")).queue_get(404, "{}");
  EXPECT_EQ(iqm::test_support::Iteration_management_smoke(true), 1);
  std::ifstream file{scratch_};
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>{file}, {}),
            std::string(other) + "\n");
  EXPECT_TRUE(http_.post_urls().empty());
}
} // namespace
