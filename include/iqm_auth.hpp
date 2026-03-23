/*
 * Copyright (c) 2025 - 2026 IQM QDMI developers
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @file
 * @brief The IQM QDMI Device Authentication.
 */

#pragma once

#include <memory>
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
 * Interface for token providers.
 */
class TokenProviderInterface {
public:
  TokenProviderInterface() = default;
  TokenProviderInterface(const TokenProviderInterface &) = default;
  TokenProviderInterface(TokenProviderInterface &&) = default;
  TokenProviderInterface &operator=(const TokenProviderInterface &) = default;
  TokenProviderInterface &operator=(TokenProviderInterface &&) = default;
  virtual ~TokenProviderInterface() = default;

  /**
   * Returns a valid access token.
   * @throws ClientAuthenticationError if acquiring the token fails.
   */
  virtual std::string get_token() = 0;
};

/**
 * Holds an external token.
 */
class ExternalToken final : public TokenProviderInterface {
public:
  explicit ExternalToken(const std::string &token);
  std::string get_token() override;

private:
  std::optional<std::string> token_;
};

/**
 * Reads token from a file.
 */
class TokensFileReader final : public TokenProviderInterface {
public:
  explicit TokensFileReader(const std::string &tokens_file);
  std::string get_token() override;

private:
  std::optional<std::string> path_;
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
   * IQM_TOKENS_FILE. Environment variables cannot be mixed with initialization
   * arguments. All parameters must come from the same source.
   */
  explicit TokenManager(
      const std::optional<std::string> &token = std::nullopt,
      const std::optional<std::string> &tokens_file = std::nullopt);

  /**
   * Returns a valid bearer token, or empty string if no user authentication has
   * been configured.
   * @param retries Number of retry attempts
   * @return Bearer token or empty string
   * @throws ClientAuthenticationError if getting the token fails
   */
  std::string get_bearer_token(int retries = 1);

private:
  std::unique_ptr<TokenProviderInterface> token_provider_;
  std::optional<std::string> access_token_;
};

} // namespace iqm
