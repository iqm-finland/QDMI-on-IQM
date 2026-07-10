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

#include "http_stub.hpp"

#include "http_client_internal.hpp"

#include <cpr/error.h>
#include <cpr/response.h>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace iqm::test_support {

namespace {

int Cpr_error_code(const cpr::ErrorCode code) { return static_cast<int>(code); }

cpr::Response Make_response(const Scripted_response &scripted) {
  cpr::Response response;
  if (scripted.connection_error) {
    response.error = cpr::Error{Cpr_error_code(cpr::ErrorCode::COULDNT_CONNECT),
                                "Failed to connect to host"};
    return response;
  }
  response.status_code = scripted.status_code;
  response.text = scripted.body;
  response.header = scripted.headers;
  return response;
}

} // namespace

HttpStub::HttpStub() {
  auto &hooks = http::internal::Get_hooks();

  hooks.get = [this](const cpr::Url &url,
                     const std::optional<cpr::Bearer> &bearer_token,
                     const cpr::Header & /*headers*/) {
    get_urls_.push_back(url.str());
    get_bearer_tokens_.push_back(bearer_token);
    if (get_responses_.empty()) {
      ADD_FAILURE() << "Unexpected GET request to '" << url
                    << "': no scripted response was queued";
      cpr::Response response;
      response.status_code = 500;
      response.url = url;
      return response;
    }
    const auto scripted = get_responses_.front();
    get_responses_.pop_front();
    auto response = Make_response(scripted);
    response.url = url;
    return response;
  };

  hooks.post = [this](const cpr::Url &url,
                      const std::optional<cpr::Bearer> &bearer_token,
                      const cpr::Header & /*headers*/,
                      const cpr::Body & /*body*/) {
    post_urls_.push_back(url.str());
    post_bearer_tokens_.push_back(bearer_token);
    if (post_responses_.empty()) {
      ADD_FAILURE() << "Unexpected POST request to '" << url
                    << "': no scripted response was queued";
      cpr::Response response;
      response.status_code = 500;
      response.url = url;
      return response;
    }
    const auto scripted = post_responses_.front();
    post_responses_.pop_front();
    auto response = Make_response(scripted);
    response.url = url;
    return response;
  };

  hooks.sleep = [this](const int seconds) {
    sleep_durations_.push_back(seconds);
  };
}

HttpStub::~HttpStub() { http::internal::Reset_hooks(); }

HttpStub &HttpStub::queue_get(const int64_t status_code, std::string body,
                              cpr::Header headers) {
  get_responses_.push_back({.status_code = status_code,
                            .body = std::move(body),
                            .connection_error = false,
                            .headers = std::move(headers)});
  return *this;
}

HttpStub &HttpStub::queue_get_connection_error() {
  get_responses_.push_back(
      {.status_code = 0, .body = {}, .connection_error = true, .headers = {}});
  return *this;
}

HttpStub &HttpStub::queue_post(const int64_t status_code, std::string body,
                               cpr::Header headers) {
  post_responses_.push_back({.status_code = status_code,
                             .body = std::move(body),
                             .connection_error = false,
                             .headers = std::move(headers)});
  return *this;
}

HttpStub &HttpStub::queue_post_connection_error() {
  post_responses_.push_back(
      {.status_code = 0, .body = {}, .connection_error = true, .headers = {}});
  return *this;
}

const std::vector<std::string> &HttpStub::get_urls() const { return get_urls_; }

const std::vector<std::optional<cpr::Bearer>> &
HttpStub::get_bearer_tokens() const {
  return get_bearer_tokens_;
}

const std::vector<std::string> &HttpStub::post_urls() const {
  return post_urls_;
}

const std::vector<std::optional<cpr::Bearer>> &
HttpStub::post_bearer_tokens() const {
  return post_bearer_tokens_;
}

size_t HttpStub::sleep_call_count() const { return sleep_durations_.size(); }

const std::vector<int> &HttpStub::sleep_durations() const {
  return sleep_durations_;
}

} // namespace iqm::test_support
