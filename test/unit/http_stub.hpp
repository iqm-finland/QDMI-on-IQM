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
 * @brief Scripts HTTP responses for tests, from single-request checks up to
 * full device-session flows.
 */

#pragma once

#include <cpr/bearer.h>
#include <cpr/cprtypes.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace iqm::test_support {

/// A single scripted low-level HTTP response used to drive the iqm::http
/// hook seam.
struct Scripted_response {
  /// HTTP status code to report (ignored when connection_error is true).
  int64_t status_code = 200;
  /// Response body to report.
  std::string body;
  /// If true, simulate a transport-level failure (e.g. connection refused)
  /// instead of returning a status code.
  bool connection_error = false;
  /// Response headers to report.
  cpr::Header headers;
};

/**
 * @brief Scripts HTTP responses so that iqm::http::Get/Get_optional/Post
 * (and everything built on top of them, up to and including the public
 * `IQM_QDMI_device_session_init`) can be exercised end-to-end without any
 * live network traffic or real time delays.
 *
 * This is the single test double for all HTTP-dependent tests: it installs
 * hooks via `iqm::http::internal::Get_hooks()` (see http_client.hpp)
 * so tests exercise the real response-code mapping and retry logic instead
 * of bypassing it behind a mocked interface.
 *
 * GET and POST responses are consumed strictly in the order they were
 * queued, matching the order requests are issued by the production code.
 * Any request beyond what has been queued triggers a non-fatal test failure
 * and returns a generic HTTP 500 response, so missing expectations are
 * caught reliably. The retry backoff delay is stubbed out (counted, not
 * slept), so tests that exercise HTTP 429 retries run instantly.
 *
 * The default hooks are restored on destruction (RAII), so each test that
 * owns an instance of this class gets an isolated, self-cleaning stub.
 */
class HttpStub {
public:
  HttpStub();
  ~HttpStub();

  HttpStub(const HttpStub &) = delete;
  HttpStub &operator=(const HttpStub &) = delete;
  HttpStub(HttpStub &&) = delete;
  HttpStub &operator=(HttpStub &&) = delete;

  /// Queue the next scripted GET response.
  HttpStub &queue_get(int64_t status_code, std::string body = {},
                      cpr::Header headers = {});
  /// Queue the next GET request to fail at the transport level.
  HttpStub &queue_get_connection_error();
  /// Queue the next scripted POST response.
  HttpStub &queue_post(int64_t status_code, std::string body = {},
                       cpr::Header headers = {});
  /// Queue the next POST request to fail at the transport level.
  HttpStub &queue_post_connection_error();

  /// URLs requested via GET, in call order.
  [[nodiscard]] const std::vector<std::string> &get_urls() const;
  /// Bearer tokens passed to GET requests, in call order.
  [[nodiscard]] const std::vector<std::optional<cpr::Bearer>> &
  get_bearer_tokens() const;
  /// URLs requested via POST, in call order.
  [[nodiscard]] const std::vector<std::string> &post_urls() const;
  /// Bearer tokens passed to POST requests, in call order.
  [[nodiscard]] const std::vector<std::optional<cpr::Bearer>> &
  post_bearer_tokens() const;
  /// Number of retry-delay ("sleep") calls triggered by HTTP 429 retries.
  [[nodiscard]] size_t sleep_call_count() const;
  /// Retry-delay durations requested by HTTP 429 handling, in call order.
  [[nodiscard]] const std::vector<int> &sleep_durations() const;

private:
  std::deque<Scripted_response> get_responses_;
  std::deque<Scripted_response> post_responses_;
  std::vector<std::string> get_urls_;
  std::vector<std::optional<cpr::Bearer>> get_bearer_tokens_;
  std::vector<std::string> post_urls_;
  std::vector<std::optional<cpr::Bearer>> post_bearer_tokens_;
  std::vector<int> sleep_durations_;
};

} // namespace iqm::test_support
