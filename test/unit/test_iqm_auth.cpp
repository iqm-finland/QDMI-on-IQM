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

#include "iqm_auth.hpp"

#include "gtest/gtest.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <string>
#include <string_view>

namespace {
int Set_env_var_raw(const char *key, const char *value) {
#ifdef _WIN32
  return _putenv_s(key, value);
#else
  return setenv(key, value, 1);
#endif
}

int Unset_env_var_raw(const char *key) {
#ifdef _WIN32
  return _putenv_s(key, "");
#else
  return unsetenv(key);
#endif
}

class ScopedEnvVar {
public:
  ScopedEnvVar(const char *key, const char *value) : key_(key) {
    if (const char *existing_value = std::getenv(key);
        existing_value != nullptr) {
      previous_value_ = existing_value;
    }
    if (value != nullptr) {
      EXPECT_EQ(Set_env_var_raw(key, value), 0);
    } else {
      EXPECT_EQ(Unset_env_var_raw(key), 0);
    }
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;
  ScopedEnvVar(ScopedEnvVar &&) = delete;
  ScopedEnvVar &operator=(ScopedEnvVar &&) = delete;

  ~ScopedEnvVar() {
    // Restore original process env var state for test isolation.
    if (previous_value_.has_value()) {
      static_cast<void>(
          Set_env_var_raw(key_.c_str(), previous_value_->c_str()));
    } else {
      static_cast<void>(Unset_env_var_raw(key_.c_str()));
    }
  }

private:
  std::string key_;
  std::optional<std::string> previous_value_;
};

class ScopedTokensFile {
public:
  ScopedTokensFile(std::string path, const std::string &access_token)
      : path_(std::move(path)) {
    std::ofstream file(path_);
    file << R"({"access_token": ")" << access_token << R"("})";
  }

  ScopedTokensFile(const ScopedTokensFile &) = delete;
  ScopedTokensFile &operator=(const ScopedTokensFile &) = delete;
  ScopedTokensFile(ScopedTokensFile &&) = delete;
  ScopedTokensFile &operator=(ScopedTokensFile &&) = delete;

  ~ScopedTokensFile() { static_cast<void>(std::remove(path_.c_str())); }

  [[nodiscard]] const std::string &path() const { return path_; }

private:
  std::string path_;
};

testing::AssertionResult
Is_bearer_token_for(const std::string &actual,
                    const std::string &expected_token) {
  constexpr std::string_view bearer_prefix = "Bearer ";
  if (!actual.starts_with(bearer_prefix)) {
    return testing::AssertionFailure() << "missing Bearer prefix";
  }
  if (actual.size() != bearer_prefix.size() + expected_token.size()) {
    return testing::AssertionFailure() << "unexpected bearer token length";
  }
  if (actual.compare(bearer_prefix.size(), std::string::npos, expected_token) !=
      0) {
    return testing::AssertionFailure() << "unexpected bearer token content";
  }
  return testing::AssertionSuccess();
}

constexpr std::string_view K_FUTURE_TOKEN =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE3ODg0MDY0MDAsIm5iZiI6MTYyMDY3NTIw"
    "MCwiaWF0IjoxNjIwNjc1MjAwfQ.signature";
} // namespace

TEST(TokenManagerTest, TimeLeftSeconds) {
  // A valid token with an expiration time in the future
  const std::string valid_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                                  "eyJleHAiOjE3ODg0MDY0MDAsIm5iZiI6MTYyMDY3NTIw"
                                  "MCwiaWF0IjoxNjIwNjc1MjAwfQ.signature";
  EXPECT_GT(iqm::TokenManager::time_left_seconds(valid_token), 0);

  // An expired token
  const std::string expired_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                                    "eyJleHAiOjE2MjA2NzUyMDAsIm5iZiI6MTYyMDY3NT"
                                    "IwMCwiaWF0IjoxNjIwNjc1MjAwfQ.signature";
  EXPECT_EQ(iqm::TokenManager::time_left_seconds(expired_token), 0);

  // An invalid token
  const std::string invalid_token = "invalid.token";
  EXPECT_EQ(iqm::TokenManager::time_left_seconds(invalid_token), 0);

  // An empty token
  EXPECT_EQ(iqm::TokenManager::time_left_seconds(""), 0);
}

TEST(TokenManagerTest, ConstructorWithExplicitToken) {
  const ScopedEnvVar env_token("IQM_TOKEN", nullptr);
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", nullptr);

  iqm::TokenManager tm(std::make_optional("my_token"), std::nullopt);
  EXPECT_TRUE(Is_bearer_token_for(tm.get_bearer_token(), "my_token"));
}

TEST(TokenManagerTest, ConstructorWithTokenFromEnvironment) {
  const ScopedEnvVar env_token("IQM_TOKEN", "my_token");
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", nullptr);

  iqm::TokenManager tm(std::nullopt, std::nullopt);
  EXPECT_TRUE(Is_bearer_token_for(tm.get_bearer_token(), "my_token"));
}

TEST(TokenManagerTest, ConstructorWithExplicitTokenOverridesEnvironment) {
  const ScopedEnvVar env_token("IQM_TOKEN", "different_token");
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", "different_file");

  iqm::TokenManager tm(std::make_optional("my_token"), std::nullopt);
  EXPECT_TRUE(Is_bearer_token_for(tm.get_bearer_token(), "my_token"));
}

TEST(TokenManagerTest, ConstructorWithExplicitTokensFile) {
  const ScopedEnvVar env_token("IQM_TOKEN", nullptr);
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", nullptr);
  const ScopedTokensFile tokens_file("test_tokens.json",
                                     std::string(K_FUTURE_TOKEN));

  iqm::TokenManager tm(std::nullopt, std::make_optional(tokens_file.path()));
  EXPECT_TRUE(
      Is_bearer_token_for(tm.get_bearer_token(), std::string(K_FUTURE_TOKEN)));
}

TEST(TokenManagerTest, ConstructorWithTokensFileFromEnvironment) {
  const ScopedEnvVar env_token("IQM_TOKEN", nullptr);
  const ScopedTokensFile tokens_file("test_tokens_env.json",
                                     std::string(K_FUTURE_TOKEN));
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE",
                                     tokens_file.path().c_str());

  iqm::TokenManager tm(std::nullopt, std::nullopt);
  EXPECT_TRUE(
      Is_bearer_token_for(tm.get_bearer_token(), std::string(K_FUTURE_TOKEN)));
}

TEST(TokenManagerTest, ConstructorWithExplicitTokensFileOverridesEnvironment) {
  const ScopedEnvVar env_token("IQM_TOKEN", "different_token");
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", "different_file");
  const ScopedTokensFile tokens_file("test_tokens_override.json",
                                     std::string(K_FUTURE_TOKEN));

  iqm::TokenManager tm(std::nullopt, std::make_optional(tokens_file.path()));
  EXPECT_TRUE(
      Is_bearer_token_for(tm.get_bearer_token(), std::string(K_FUTURE_TOKEN)));
}

TEST(TokenManagerTest, ConstructorWithInvalidExplicitParameters) {
  const ScopedEnvVar env_token("IQM_TOKEN", nullptr);
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", nullptr);

  EXPECT_THROW(iqm::TokenManager(std::make_optional("token"),
                                 std::make_optional("file")),
               iqm::ClientConfigurationError);
}

TEST(TokenManagerTest, ConstructorWithInvalidEnvironmentParameters) {
  const ScopedEnvVar env_token("IQM_TOKEN", "token");
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", "file");

  EXPECT_THROW(iqm::TokenManager(), iqm::ClientConfigurationError);
}

TEST(TokenManagerTest, GetBearerTokenNoProvider) {
  const ScopedEnvVar env_token("IQM_TOKEN", nullptr);
  const ScopedEnvVar env_tokens_file("IQM_TOKENS_FILE", nullptr);

  iqm::TokenManager tm;
  EXPECT_TRUE(tm.get_bearer_token().empty());
}

TEST(TokenManagerTest, TimeLeftSecondsInvalidJson) {
  // A token with invalid JSON in the body
  const std::string invalid_json_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                                         "aW52YWxpZCBqc29u."
                                         "signature";
  EXPECT_EQ(iqm::TokenManager::time_left_seconds(invalid_json_token), 0);
}

TEST(ExternalTokenTest, GetToken) {
  iqm::ExternalToken et("my_token");
  EXPECT_EQ(et.get_token(), "my_token");
}

TEST(TokensFileReaderTest, GetToken) {
  // Create a temporary tokens file
  const std::string filename = "test_tokens.json";
  std::ofstream file(filename);
  file
      << R"({"access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3ODg0MDY0MDAsIm5iZiI6MTYyMDY3NTIwMCwiaWF0IjoxNjIwNjc1MjAwfQ.signature"})";
  file.close();

  iqm::TokensFileReader tfr(filename);
  EXPECT_EQ(tfr.get_token(), "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                             "eyJleHAiOjE3ODg0MDY0MDAsIm5iZiI6MTYyMDY3NTIwMCwia"
                             "WF0IjoxNjIwNjc1MjAwfQ.signature");

  // Clean up the temporary file
  std::remove(filename.c_str());
}

TEST(TokensFileReaderTest, GetTokenExpired) {
  // Create a temporary tokens file with an expired token
  const std::string filename = "test_tokens_expired.json";
  std::ofstream file(filename);
  file
      << R"({"access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2MjA2NzUyMDAsIm5iZiI6MTYyMDY3NTIwMCwiaWF0IjoxNjIwNjc1MjAwfQ.signature"})";
  file.close();

  iqm::TokensFileReader tfr(filename);
  EXPECT_THROW(tfr.get_token(), iqm::ClientAuthenticationError);

  // Clean up the temporary file
  std::remove(filename.c_str());
}

TEST(TokensFileReaderTest, GetTokenInvalidJson) {
  // Create a temporary tokens file with invalid JSON
  const std::string filename = "test_tokens_invalid.json";
  std::ofstream file(filename);
  file << "{invalid_json}";
  file.close();

  iqm::TokensFileReader tfr(filename);
  EXPECT_THROW(tfr.get_token(), iqm::ClientAuthenticationError);

  // Clean up the temporary file
  std::remove(filename.c_str());
}

TEST(TokensFileReaderTest, GetTokenFileNotFound) {
  iqm::TokensFileReader tfr("non_existent_file.json");
  EXPECT_THROW(tfr.get_token(), iqm::ClientAuthenticationError);
}

TEST(TokensFileReaderTest, GetTokenNoAccessToken) {
  // Create a temporary tokens file without an access_token key
  const std::string filename = "test_tokens_no_access_token.json";
  std::ofstream file(filename);
  file << R"({"other_key": "some_value"})";
  file.close();

  iqm::TokensFileReader tfr(filename);
  EXPECT_THROW(tfr.get_token(), iqm::ClientAuthenticationError);

  // Clean up the temporary file
  std::remove(filename.c_str());
}

TEST(TokensFileReaderTest, GetTokenEmptyFile) {
  // Create an empty temporary tokens file
  const std::string filename = "test_tokens_empty.json";
  std::ofstream file(filename);
  file.close();

  iqm::TokensFileReader tfr(filename);
  EXPECT_THROW(tfr.get_token(), iqm::ClientAuthenticationError);

  // Clean up the temporary file
  std::remove(filename.c_str());
}
