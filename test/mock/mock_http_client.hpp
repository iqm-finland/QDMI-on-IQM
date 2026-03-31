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
 * @brief A mock HTTP client for testing.
 */

#pragma once

#include "http_client.hpp"

#include <gmock/gmock.h>
#include <string>

namespace iqm {
class MockHttpClient final : public IHttpClient {
public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  MOCK_METHOD(int, get,
              (const std::string &url, const std::string &bearer_token,
               std::string &response),
              (override));

  // NOLINTNEXTLINE(readability-identifier-naming)
  MOCK_METHOD(int, post,
              (const std::string &url, const std::string &bearer_token,
               std::string &response, const std::string &data,
               const std::string &extra_header),
              (override));
};
} // namespace iqm
