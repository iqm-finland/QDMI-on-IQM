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

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace iqm::test_support {

namespace {

http::internal::Raw_response
Make_raw_response(const Scripted_response &scripted) {
  if (scripted.connection_error) {
    return {.status_code = 0,
            .body = {},
            .error_message = "Failed to connect to host"};
  }
  return {.status_code = scripted.status_code,
          .body = scripted.body,
          .error_message = {}};
}

} // namespace

HttpStub::HttpStub() {
  auto &hooks = http::internal::Get_hooks();

  hooks.get = [this](const std::string &url,
                     const std::string & /*bearer_token*/) {
    get_urls_.push_back(url);
    if (get_responses_.empty()) {
      ADD_FAILURE() << "Unexpected GET request to '" << url
                    << "': no scripted response was queued";
      return http::internal::Raw_response{
          .status_code = 500, .body = {}, .error_message = {}};
    }
    const auto scripted = get_responses_.front();
    get_responses_.pop_front();
    return Make_raw_response(scripted);
  };

  hooks.post = [this](const std::string &url,
                      const std::string & /*bearer_token*/,
                      const std::string & /*data*/,
                      const std::string & /*extra_header*/) {
    post_urls_.push_back(url);
    if (post_responses_.empty()) {
      ADD_FAILURE() << "Unexpected POST request to '" << url
                    << "': no scripted response was queued";
      return http::internal::Raw_response{
          .status_code = 500, .body = {}, .error_message = {}};
    }
    const auto scripted = post_responses_.front();
    post_responses_.pop_front();
    return Make_raw_response(scripted);
  };

  hooks.sleep = [this](int /*seconds*/) { ++sleep_call_count_; };
}

HttpStub::~HttpStub() { http::internal::Reset_hooks(); }

HttpStub &HttpStub::queue_get(const int64_t status_code, std::string body) {
  get_responses_.push_back({.status_code = status_code,
                            .body = std::move(body),
                            .connection_error = false});
  return *this;
}

HttpStub &HttpStub::queue_get_connection_error() {
  get_responses_.push_back(
      {.status_code = 0, .body = {}, .connection_error = true});
  return *this;
}

HttpStub &HttpStub::queue_post(const int64_t status_code, std::string body) {
  post_responses_.push_back({.status_code = status_code,
                             .body = std::move(body),
                             .connection_error = false});
  return *this;
}

HttpStub &HttpStub::queue_post_connection_error() {
  post_responses_.push_back(
      {.status_code = 0, .body = {}, .connection_error = true});
  return *this;
}

const std::vector<std::string> &HttpStub::get_urls() const { return get_urls_; }

const std::vector<std::string> &HttpStub::post_urls() const {
  return post_urls_;
}

size_t HttpStub::sleep_call_count() const { return sleep_call_count_; }

} // namespace iqm::test_support
