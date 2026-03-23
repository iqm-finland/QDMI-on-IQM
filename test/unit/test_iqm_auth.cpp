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

#include "iqm_auth.hpp"

#include "gtest/gtest.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <string>

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

TEST(TokenManagerTest, ConstructorWithToken) {
  iqm::TokenManager tm(std::make_optional("my_token"), std::nullopt);
  EXPECT_EQ(tm.get_bearer_token(), "Bearer my_token");

  setenv("IQM_TOKEN", "my_token", 1);
  // Test with only the env var
  tm = iqm::TokenManager(std::nullopt, std::nullopt);
  EXPECT_EQ(tm.get_bearer_token(), "Bearer my_token");

  // Test with parameter and env var set to the same value --> expected to work
  tm = iqm::TokenManager(std::make_optional("my_token"), std::nullopt);
  EXPECT_EQ(tm.get_bearer_token(), "Bearer my_token");

  // Test with the env var set to a different value --> expected to fail
  setenv("IQM_TOKEN", "different_token", 1);
  EXPECT_THROW(iqm::TokenManager(std::make_optional("my_token"), std::nullopt),
               iqm::ClientConfigurationError);
  unsetenv("IQM_TOKEN");
}

TEST(TokenManagerTest, ConstructorWithTokensFile) {
  // Create a temporary tokens file
  const std::string filename = "test_tokens.json";
  std::ofstream file(filename);
  // Create a valid token with an expiration time in the future
  const std::string future_token =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJleHAiOjE3ODg0MDY0MDAsIm5iZiI6MTYyMDY3NTIw"
      "MCwiaWF0IjoxNjIwNjc1MjAwfQ.signature";
  file << R"({"access_token": ")" << future_token << R"("})";
  file.close();

  iqm::TokenManager tm(std::nullopt, std::make_optional(filename));
  EXPECT_EQ(tm.get_bearer_token(), "Bearer " + future_token);

  setenv("IQM_TOKENS_FILE", filename.c_str(), 1);
  // Test with only the env var
  tm = iqm::TokenManager(std::nullopt, std::nullopt);
  EXPECT_EQ(tm.get_bearer_token(), "Bearer " + future_token);

  // Test with parameter and env var set to the same value --> expected to work
  tm = iqm::TokenManager(std::nullopt, std::make_optional(filename));
  EXPECT_EQ(tm.get_bearer_token(), "Bearer " + future_token);

  // Test with the env var set to a different value --> expected to fail
  setenv("IQM_TOKENS_FILE", "different_file_token", 1);
  EXPECT_THROW(iqm::TokenManager(std::nullopt, std::make_optional(filename)),
               iqm::ClientConfigurationError);
  unsetenv("IQM_TOKENS_FILE");

  // Clean up the temporary file
  std::remove(filename.c_str());
}

TEST(TokenManagerTest, ConstructorWithInvalidParameters) {
  EXPECT_THROW(iqm::TokenManager(std::make_optional("token"),
                                 std::make_optional("file")),
               iqm::ClientConfigurationError);
}

TEST(TokenManagerTest, GetBearerTokenNoProvider) {
  iqm::TokenManager tm;
  EXPECT_EQ(tm.get_bearer_token(), "");
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
