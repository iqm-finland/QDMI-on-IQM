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
 * @brief Internal test seam for iqm::http, shared between the implementation
 * and its unit tests.
 *
 * This header is **not** part of the public API.
 */

#pragma once

#include <functional>
#include <optional>

// Forward declarations for cpr types used in the hooks.
namespace cpr {
class Url;
class Bearer;
class Body;
class Response;
} // namespace cpr

namespace iqm::http::internal {

/**
 * @brief Function hooks used to intercept HTTP calls and retry delays.
 *
 * Tests override these to exercise get()/post()/retry logic without a live
 * network connection or real time delays. Production code uses the defaults
 * installed by Get_hooks(), which perform real requests and real sleeps.
 */
struct Hooks {
  /// Hook for GET requests.
  std::function<cpr::Response(const cpr::Url &url,
                              const std::optional<cpr::Bearer> &bearer_token,
                              const cpr::Header &headers)>
      get{};
  /// Hook for POST requests.
  std::function<cpr::Response(
      const cpr::Url &url, const std::optional<cpr::Bearer> &bearer_token,
      const cpr::Header &headers, const cpr::Body &body)>
      post{};
  /// Hook for the retry backoff delay, given a delay in seconds.
  std::function<void(int)> sleep{};
};

/// Access the mutable, process-wide hook set.
Hooks &Get_hooks();

/// Restore the default (real) hooks. Used by tests to clean up after
/// themselves.
void Reset_hooks();

} // namespace iqm::http::internal
