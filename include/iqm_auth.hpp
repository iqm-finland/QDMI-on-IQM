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
 * @brief The IQM QDMI Device Authentication.
 */

#pragma once

#include <cpr/bearer.h>
#include <optional>
#include <stdexcept>
#include <string>

namespace iqm {

// Constants
constexpr auto REFRESH_MARGIN_SECONDS = 60;

/**
 * Authentication related errors.
 */
class ClientAuthenticationError final : public std::runtime_error {
public:
  explicit ClientAuthenticationError(const std::string &message)
      : std::runtime_error(message) {}
};

/**
 * Configuration related errors.
 */
class ClientConfigurationError final : public std::runtime_error {
public:
  explicit ClientConfigurationError(const std::string &message)
      : std::runtime_error(message) {}
};

/**
 * TokenManager manages the access token required for user authentication.
 */
class TokenManager {
public:
  /**
   * Check how much time is left until the token expires.
   * @param token JWT token
   * @return Time left on token in seconds
   */
  static int time_left_seconds(const std::string &token);

  /**
   * Constructor with explicit parameters.
   * @param token Long-lived IQM token in plain text format
   * @param tokens_file Path to a tokens file used for authentication
   *
   * Parameters can also be read from environment variables IQM_TOKEN or
   * IQM_TOKENS_FILE. Explicit initialization arguments take precedence over
   * environment variables. Environment variables are only used when neither
   * explicit authentication parameter is provided.
   */
  explicit TokenManager(
      const std::optional<std::string> &token = std::nullopt,
      const std::optional<std::string> &tokens_file = std::nullopt);

  /**
   * Returns a bearer token, or no value if no user
   * authentication has been configured.
   * @param retries Number of retry attempts
   * @return Bearer token for CPR requests
   * @throws ClientAuthenticationError if getting the token fails
   */
  std::optional<cpr::Bearer> get_bearer_token(int retries = 1);

private:
  enum class TokenSource { NONE, EXTERNAL_TOKEN, TOKENS_FILE };

  TokenSource token_source_ = TokenSource::NONE;
  std::optional<std::string> token_source_value_;
  std::optional<std::string> access_token_;
};

} // namespace iqm
