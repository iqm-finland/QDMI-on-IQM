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

#include <cpr/connection_pool.h>
#include <cpr/curlholder.h>
#include <cpr/session.h>
#include <cstddef>
#include <curl/curl.h>
#include <curl/easy.h>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

namespace {
std::size_t Discard(char * /*data*/, std::size_t size, std::size_t count,
                    void * /*user*/) {
  return size * count;
}
} // namespace

int main(int argc, char **argv) try {
  std::cout << curl_version() << '\n';
  if (argc == 2) {
    const auto response =
        iqm::http::Get(cpr::Url{argv[1]}, std::nullopt, cpr::ConnectionPool{},
                       std::chrono::seconds{10});
    std::cout << response.status_code << ' '
              << static_cast<int>(response.error.code) << ' '
              << response.error.message << '\n';
    return response.error ? 1 : 0;
  }
  if (argc != 4 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    return 1;
  }
  int result = 1;
  {
    cpr::Session session;
    auto *handle = session.GetCurlHolder()->handle;
    const auto control_bundle = std::string{argv[3]} + "/control.pem";
    // Seed an alternate trust directory without changing the host's CA store.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    const auto capath = curl_easy_setopt(handle, CURLOPT_CAPATH, argv[3]);
    if (capath == CURLE_NOT_BUILT_IN) {
      std::cout << "TLS backend does not support CA directories\n";
      return 125;
    }
    if (capath != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_CAINFO, control_bundle.c_str()) !=
            CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_URL, argv[1]) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_NOPROXY, "*") != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, Discard) != CURLE_OK) {
      return 1;
    }
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
    try {
      if (std::string{argv[2]} != "-") {
        iqm::http::internal::Configure_ca_bundle(session, argv[2]);
      }
      result = static_cast<int>(curl_easy_perform(handle));
      std::cout << result << ' ' << session.GetCurlHolder()->error.data()
                << '\n';
    } catch (const std::exception &) {
      return 1;
    }
  }
  curl_global_cleanup();
  return result;
} catch (const std::exception &) {
  return 1;
}
