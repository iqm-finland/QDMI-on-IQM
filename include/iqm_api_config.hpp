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
 * @brief The IQM QDMI Device API Configuration.
 */

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace iqm {

/**
 * Supported API endpoints for the IQM Server API.
 */
enum class API_ENDPOINT : uint8_t {
  // Job management endpoints
  SUBMIT_CIRCUIT_JOB,
  GET_JOB_STATUS,
  GET_JOB_ARTIFACT_MEASUREMENT_COUNTS,
  GET_JOB_ARTIFACT_MEASUREMENTS,
  CANCEL_JOB,

  // Quantum computer endpoints
  GET_QUANTUM_COMPUTERS,
  GET_STATIC_QUANTUM_ARCHITECTURE,
  GET_DYNAMIC_QUANTUM_ARCHITECTURE,
  GET_CALIBRATION_SET_QUALITY_METRICS,

  // Calibration job endpoints (may not be available)
  COCOS_HEALTH,
  SUBMIT_CALIBRATION_JOB,
  GET_CALIBRATION_JOB_STATUS,
  ABORT_CALIBRATION_JOB,
};

/**
 * Provides supported API endpoints for the unified IQM Server API.
 */
class APIConfig {
public:
  /**
   * Constructor.
   * @param iqm_server_url URL of the IQM server.
   */
  explicit APIConfig(std::string iqm_server_url);

  /**
   * Builds a URL for the given endpoint.
   * @param endpoint API endpoint.
   * @param args Arguments to be passed to the URL.
   * @return URL for the given endpoint.
   */
  template <typename... Args>
  std::string url(const API_ENDPOINT endpoint, Args &&...args) const {
    const auto url = get_url(endpoint);
    const std::string formatted_url =
        format_url(url, std::forward<Args>(args)...);
    return join_paths(iqm_server_url_, formatted_url);
  }

private:
  std::string iqm_server_url_;

  /**
   * Get URL for the given endpoint.
   */
  static std::string get_url(API_ENDPOINT endpoint);

  /**
   * Joins path components.
   * @param base Base URL.
   * @param path Path to join.
   * @return Joined path.
   */
  static std::string join_paths(const std::string &base,
                                const std::string &path);

  /**
   * Format URL with parameters.
   * Base case for the template recursion.
   */
  static std::string format_url(const std::string &url) { return url; }

  /**
   * Format URL with parameters.
   * @param url URL template with %s placeholders.
   * @param arg First argument to format.
   * @param args Rest of the arguments.
   * @return Formatted URL.
   */
  template <typename T, typename... Args>
  static std::string format_url(const std::string &url, T &&arg,
                                Args &&...args) {
    const auto pos = url.find("%s");
    if (pos == std::string::npos) {
      return url;
    }
    std::string new_url = url.substr(0, pos) +
                          format_url_arg(std::forward<T>(arg)) +
                          url.substr(pos + 2);
    return format_url(new_url, std::forward<Args>(args)...);
  }

  // Helper to convert argument to string (for numeric types)
  template <typename T>
  static std::string format_url_arg(T &&arg)
    requires(std::is_arithmetic_v<std::decay_t<T>>)
  {
    return std::to_string(std::forward<T>(arg));
  }

  // Overload for std::string
  static std::string format_url_arg(const std::string &arg) { return arg; }

  // Overload for const char*
  static std::string format_url_arg(const char *arg) { return {arg}; }

  /**
   * Specialization for string arguments.
   */
  template <typename... Args>
  static std::string format_url(const std::string &url, const std::string &arg,
                                Args &&...args) {
    const auto pos = url.find("%s");
    if (pos == std::string::npos) {
      return url;
    }

    std::string new_url = url.substr(0, pos) + arg + url.substr(pos + 2);
    return format_url(new_url, std::forward<Args>(args)...);
  }

  /**
   * Specialization for string arguments.
   */
  template <typename... Args>
  static std::string format_url(const std::string &url, std::string &arg,
                                Args &&...args) {
    return format_url(url, static_cast<const std::string &>(arg),
                      std::forward<Args>(args)...);
  }
};
} // namespace iqm
