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

Logger::Logger()
    : current_level_(LOG_LEVEL::ERROR), output_stream_(&std::cerr) {
  if (const char *level_str = std::getenv("IQM_CPP_API_LOG_LEVEL")) {
    if (const std::string level = level_str; level == "INFO") {
      current_level_ = LOG_LEVEL::INFO;
    } else if (level == "DEBUG") {
      current_level_ = LOG_LEVEL::DEBUG;
    } else if (level != "ERROR") {
      current_level_ = LOG_LEVEL::NONE;
    }
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
