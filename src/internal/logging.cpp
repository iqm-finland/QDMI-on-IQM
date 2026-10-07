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

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace iqm {

Logger &Logger::get_instance() {
  static Logger instance;
  return instance;
}

namespace {

/// The environment variable that names the log level.
constexpr auto LOG_LEVEL_VARIABLE = "IQM_LOG_LEVEL";
/// The variable LOG_LEVEL_VARIABLE replaced, still read as a fallback.
constexpr auto DEPRECATED_LOG_LEVEL_VARIABLE = "IQM_CPP_API_LOG_LEVEL";

/**
 * @brief Read an environment variable, treating an empty value as unset.
 *
 * Job schedulers and container runtimes routinely export a variable with an
 * empty value, which must not count as a level the caller asked for.
 *
 * @param key Name of the environment variable.
 * @return The value, or `nullptr` when it is unset or empty.
 */
const char *Non_empty_env(const char *key) {
  const char *value = std::getenv(key);
  if (value == nullptr || *value == '\0') {
    return nullptr;
  }
  return value;
}

/**
 * @brief Map a level name onto the level it selects.
 * @param name The name given in the environment.
 * @return The level, or LOG_LEVEL::NONE when @p name matches no level.
 */
LOG_LEVEL Level_from_name(const std::string &name) {
  if (name == "ERROR") {
    return LOG_LEVEL::ERROR;
  }
  if (name == "INFO") {
    return LOG_LEVEL::INFO;
  }
  if (name == "DEBUG") {
    return LOG_LEVEL::DEBUG;
  }
  return LOG_LEVEL::NONE;
}

} // namespace

LOG_LEVEL Logger::level_from_environment() {
  if (const char *level = Non_empty_env(LOG_LEVEL_VARIABLE); level != nullptr) {
    return Level_from_name(level);
  }
  if (const char *level = Non_empty_env(DEPRECATED_LOG_LEVEL_VARIABLE);
      level != nullptr) {
    return Level_from_name(level);
  }
  return LOG_LEVEL::ERROR;
}

std::string Logger::deprecation_notice() {
  if (Non_empty_env(LOG_LEVEL_VARIABLE) != nullptr ||
      Non_empty_env(DEPRECATED_LOG_LEVEL_VARIABLE) == nullptr) {
    return {};
  }
  return std::string{DEPRECATED_LOG_LEVEL_VARIABLE} +
         " is deprecated and will be removed in a future release; set " +
         LOG_LEVEL_VARIABLE + " instead";
}

Logger::Logger()
    : current_level_(level_from_environment()), output_stream_(&std::cerr) {
  // Reported through this instance rather than the LOG_ERROR macro, which
  // would re-enter get_instance() while it is still constructing.
  if (const auto notice = deprecation_notice(); !notice.empty()) {
    error(notice);
  }
}

LOG_LEVEL Logger::get_level() const { return current_level_; }

void Logger::set_level(const LOG_LEVEL level) { current_level_ = level; }

void Logger::set_output(std::ostream &stream) { output_stream_ = &stream; }

void Logger::log(const std::string &level, const std::string &message) {
  const std::scoped_lock lock(mutex_);
  const auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  *output_stream_ << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
                  << " " << level << " " << message << '\n';
}

void Logger::error(const std::string &message) {
  if (current_level_ >= LOG_LEVEL::ERROR) {
    log("ERROR", message);
  }
}

void Logger::info(const std::string &message) {
  if (current_level_ >= LOG_LEVEL::INFO) {
    log("INFO", message);
  }
}

void Logger::debug(const std::string &message) {
  if (current_level_ >= LOG_LEVEL::DEBUG) {
    log("DEBUG", message);
  }
}

} // namespace iqm
