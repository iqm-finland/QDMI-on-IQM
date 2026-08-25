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
 * @brief Sets an environment variable for the duration of a test scope.
 */

#pragma once

#include <cstdlib>
#include <gtest/gtest.h>
#include <optional>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers)
#include <string>

namespace iqm::test_support {

inline int Set_env_var_raw(const char *key, const char *value) {
#ifdef _WIN32
  return _putenv_s(key, value);
#else
  return setenv(key, value, 1);
#endif
}

inline int Unset_env_var_raw(const char *key) {
#ifdef _WIN32
  return _putenv_s(key, "");
#else
  return unsetenv(key);
#endif
}

/**
 * @brief Sets an environment variable and restores its previous state on
 * destruction, so tests that depend on the environment stay isolated.
 */
class ScopedEnvVar {
public:
  /**
   * @param key Name of the environment variable.
   * @param value Value to set, or nullptr to unset the variable.
   */
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
    if (previous_value_.has_value()) {
      static_cast<void>(
          Set_env_var_raw(key_.c_str(), previous_value_->c_str()));
    } else {
      static_cast<void>(Unset_env_var_raw(key_.c_str()));
    }
  }

private:
  /// Name of the environment variable this guard owns.
  std::string key_;
  /// Value to restore, or nullopt when the variable was unset.
  std::optional<std::string> previous_value_;
};

} // namespace iqm::test_support
