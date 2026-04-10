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
 * @brief Implementation of the IQM QDMI Device Authentication.
 */

#include "iqm_auth.hpp"

#include "logging.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace iqm {

namespace {
// Helper for base64 decoding
std::string Base64_decode(const std::string &encoded_string) {
  if (encoded_string.empty()) {
    return {};
  }

  // Determine output length
  size_t padding = 0;
  if (!encoded_string.empty()) {
    if (encoded_string[encoded_string.length() - 1] == '=') {
      padding++;
    }
    if (encoded_string.length() > 1 &&
        encoded_string[encoded_string.length() - 2] == '=') {
      padding++;
    }
  }

  const size_t outlen = ((encoded_string.length() * 3) / 4) - padding;
  std::vector<unsigned char> decoded(outlen);

  static constexpr auto DECODE_TABLE = std::array{
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
      7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
      25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1};

  size_t i = 0;
  size_t j = 0;
  uint32_t sextet_a = 0;
  uint32_t sextet_b = 0;
  uint32_t sextet_c = 0;
  uint32_t sextet_d = 0;
  uint32_t triple = 0;

  for (i = 0, j = 0; i < encoded_string.length(); i += 4) {
    sextet_a =
        encoded_string[i] == '='
            ? 0U & i
            : static_cast<uint32_t>(
                  DECODE_TABLE[static_cast<unsigned char>(encoded_string[i])]);
    sextet_b =
        i + 1 >= encoded_string.length() || encoded_string[i + 1] == '='
            ? 0U & i
            : static_cast<uint32_t>(DECODE_TABLE[static_cast<unsigned char>(
                  encoded_string[i + 1])]);
    sextet_c =
        i + 2 >= encoded_string.length() || encoded_string[i + 2] == '='
            ? 0U & i
            : static_cast<uint32_t>(DECODE_TABLE[static_cast<unsigned char>(
                  encoded_string[i + 2])]);
    sextet_d =
        i + 3 >= encoded_string.length() || encoded_string[i + 3] == '='
            ? 0U & i
            : static_cast<uint32_t>(DECODE_TABLE[static_cast<unsigned char>(
                  encoded_string[i + 3])]);

    triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;

    if (j < outlen) {
      decoded[j++] = (triple >> 16) & 0xFF;
    }
    if (j < outlen) {
      decoded[j++] = (triple >> 8) & 0xFF;
    }
    if (j < outlen) {
      decoded[j++] = triple & 0xFF;
    }
  }

  return {decoded.begin(), decoded.end()};
}

// Helper to get environment variables
std::optional<std::string> Get_env_var(const std::string &name) {
  if (const char *val = std::getenv(name.c_str()); val != nullptr) {
    return std::string(val);
  }
  return std::nullopt;
}
} // namespace

//
// TokenManager implementation
//

int TokenManager::time_left_seconds(const std::string &token) {
  if (token.empty()) {
    return 0;
  }

  // Split token by dots
  std::vector<std::string> parts;
  size_t start = 0;
  size_t end = token.find('.');

  while (end != std::string::npos) {
    parts.push_back(token.substr(start, end - start));
    start = end + 1;
    end = token.find('.', start);
  }

  // Add the last part
  parts.push_back(token.substr(start));

  if (parts.size() != 3) {
    return 0;
  }

  try {
    // Add padding for base64 decode if needed
    std::string body = parts[1];
    while (body.length() % 4 != 0) {
      body += '=';
    }

    // Decode body
    const auto decoded = Base64_decode(body);

    // Parse JSON
    const auto json =
        nlohmann::json::parse(decoded); // NOLINT(misc-include-cleaner)

    // Get expiration time
    const int exp_time = json.value("exp", 0);
    return std::max(0, exp_time - static_cast<int>(std::time(nullptr)));
  } catch (const std::exception &e) {
    LOG_DEBUG("Failed to parse token body: " + std::string(e.what()));
    return 0;
  }
}

TokenManager::TokenManager(const std::optional<std::string> &token,
                           const std::optional<std::string> &tokens_file)
    : token_provider_(nullptr), access_token_(std::nullopt) {
  std::map<std::string, std::string> auth_parameters;

  // Check for environment variables
  const auto env_token = Get_env_var("IQM_TOKEN");
  const auto env_tokens_file = Get_env_var("IQM_TOKENS_FILE");

  if (env_token.has_value() && token.has_value() && *env_token != *token) {
    throw ClientConfigurationError(
        "Authentication token given both as an initialization argument and as "
        "an environment variable IQM_TOKEN with different values."
        "Parameter sources must not be mixed.");
  }

  if (env_tokens_file.has_value() && tokens_file.has_value() &&
      *env_tokens_file != *tokens_file) {
    throw ClientConfigurationError(
        "Authentication tokens file given both as an initialization argument "
        "and as an environment variable IQM_TOKENS_FILE with different values. "
        "Parameter sources must not be mixed.");
  }

  if (env_token.has_value()) {
    auth_parameters["token"] = *env_token;
  } else if (token.has_value()) {
    auth_parameters["token"] = *token;
  }

  if (env_tokens_file.has_value()) {
    auth_parameters["tokens_file"] = *env_tokens_file;
  } else if (tokens_file.has_value()) {
    auth_parameters["tokens_file"] = *tokens_file;
  }

  // Initialize appropriate token provider
  if (auth_parameters.empty()) {
    token_provider_ = nullptr;
  } else if (auth_parameters.size() == 1 && auth_parameters.contains("token")) {
    // This is not necessarily a JWT token
    token_provider_ = std::make_unique<ExternalToken>(auth_parameters["token"]);
  } else if (auth_parameters.size() == 1 &&
             auth_parameters.contains("tokens_file")) {
    token_provider_ =
        std::make_unique<TokensFileReader>(auth_parameters["tokens_file"]);
  } else {
    std::string keys;
    for (const auto &key : auth_parameters | std::views::keys) {
      if (!keys.empty()) {
        keys += ", ";
      }
      keys += key;
    }
    throw ClientConfigurationError("Missing authentication parameters, neither "
                                   "token or tokens_file is available: " +
                                   keys);
  }
}

std::string TokenManager::get_bearer_token(const int retries) {
  if (!token_provider_) {
    return ""; // Authentication is not used
  }

  // Use the existing access token if it is still valid
  if (access_token_.has_value() &&
      time_left_seconds(*access_token_) > REFRESH_MARGIN_SECONDS) {
    return "Bearer " + *access_token_;
  }

  // Otherwise, get a new access token from token provider
  try {
    access_token_ = token_provider_->get_token();
    return "Bearer " + *access_token_;
  } catch (const ClientAuthenticationError &e) {
    LOG_ERROR("Failed to get access token: " + std::string(e.what()));
    if (retries < 1) {
      throw;
    }
  }

  // Try again
  LOG_INFO("Retrying to get access token");
  return get_bearer_token(retries - 1);
}

//
// ExternalToken implementation
//

ExternalToken::ExternalToken(const std::string &token) : token_(token) {}

std::string ExternalToken::get_token() {
  if (!token_) {
    throw ClientAuthenticationError("No external token available");
  }
  return *token_;
}

//
// TokensFileReader implementation
//

TokensFileReader::TokensFileReader(const std::string &tokens_file)
    : path_(tokens_file) {}

std::string TokensFileReader::get_token() {
  try {
    if (!path_) {
      throw ClientAuthenticationError("No tokens file available");
    }
    LOG_INFO("Reading tokens from file: " + *path_);
    const std::ifstream file(*path_);
    if (!file.good()) {
      throw ClientAuthenticationError("Failed to open tokens file: " + *path_);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string raw_data = buffer.str();
    auto json_data = nlohmann::json::parse(raw_data);

    std::string token = json_data.value("access_token", "");
    if (TokenManager::time_left_seconds(token) <= 0) {
      throw ClientAuthenticationError(
          "Access token in file has expired or is not valid");
    }

    return token;
  } catch (const nlohmann::json::exception &e) {
    throw ClientAuthenticationError("Failed to parse tokens file: " +
                                    std::string(e.what()));
  } catch (const std::exception &e) {
    if (path_) {
      throw ClientAuthenticationError(
          "Failed to read access token from file '" + *path_ +
          "': " + e.what());
    }
    throw ClientAuthenticationError("Failed to read access token from file: " +
                                    std::string(e.what()));
  }
}

} // namespace iqm
