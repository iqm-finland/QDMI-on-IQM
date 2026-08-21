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

#include "logging.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <string>

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
} // namespace

// NOTE: The logger is a singleton, and its own level is set on the first call
// to get_instance(). Because gtest runs all tests in a single process, the
// environment is exercised through level_from_environment() rather than
// through repeated construction of the singleton.

TEST(LoggingTest, DefaultLogLevel) {
  std::stringstream log_stream{};
  auto &logger = iqm::Logger::get_instance();
  logger.set_level(iqm::LOG_LEVEL::ERROR); // Ensure log level is ERROR
  logger.set_output(log_stream);

  // Assuming default log level is ERROR
  LOG_DEBUG("debug message");
  EXPECT_TRUE(log_stream.str().empty());

  LOG_INFO("info message");
  EXPECT_TRUE(log_stream.str().empty());

  log_stream.str(""); // Clear the stream
  LOG_ERROR("error message");
  EXPECT_NE(log_stream.str().find("ERROR error message"), std::string::npos);

  // Restore original output stream
  logger.set_output(std::cerr);
}

TEST(LoggingTest, LogLevelIsReadFromTheEnvironment) {
  const ScopedEnvVar deprecated_variable("IQM_CPP_API_LOG_LEVEL", nullptr);
  const auto level_for = [](const char *value) {
    const ScopedEnvVar variable("IQM_LOG_LEVEL", value);
    return iqm::Logger::level_from_environment();
  };

  EXPECT_EQ(level_for(nullptr), iqm::LOG_LEVEL::ERROR);
  EXPECT_EQ(level_for("ERROR"), iqm::LOG_LEVEL::ERROR);
  EXPECT_EQ(level_for("INFO"), iqm::LOG_LEVEL::INFO);
  EXPECT_EQ(level_for("DEBUG"), iqm::LOG_LEVEL::DEBUG);
  EXPECT_EQ(level_for("verbose"), iqm::LOG_LEVEL::NONE);
}

TEST(LoggingTest,
     DeprecatedLogLevelVariableOnlyAppliesWhenTheCurrentOneIsUnset) {
  const ScopedEnvVar deprecated_variable("IQM_CPP_API_LOG_LEVEL", "DEBUG");

  {
    const ScopedEnvVar variable("IQM_LOG_LEVEL", nullptr);
    EXPECT_EQ(iqm::Logger::level_from_environment(), iqm::LOG_LEVEL::DEBUG);
  }

  const ScopedEnvVar variable("IQM_LOG_LEVEL", "INFO");
  EXPECT_EQ(iqm::Logger::level_from_environment(), iqm::LOG_LEVEL::INFO);
}
