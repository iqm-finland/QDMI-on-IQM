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
 * @brief A simple logger for the IQM QDMI device.
 */

#pragma once

#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

namespace iqm {

/**
 * Logging level for the IQM QDMI device.
 */
enum class LOG_LEVEL : uint8_t {
  /// No logging.
  NONE = 0,
  /// Log only errors.
  ERROR,
  /// Log errors and info.
  INFO,
  /// Log errors, info, and debug messages.
  DEBUG
};

/**
 * A simple logger for the IQM QDMI device.
 */
class Logger {
public:
  /**
   * Get the singleton instance of the logger.
   * @return The logger instance.
   */
  static Logger &get_instance();

  /**
   * Get the current logging level.
   * @return The current logging level.
   */
  [[nodiscard]] LOG_LEVEL get_level() const;

  /**
   * Set the logging level.
   * @param level The logging level to set.
   */
  void set_level(LOG_LEVEL level);

  /**
   * Set the output stream for the logger.
   * @param stream The output stream to use.
   */
  void set_output(std::ostream &stream);

  /**
   * Log an error message.
   * @param message The message to log.
   */
  void error(const std::string &message);

  /**
   * Log an info message.
   * @param message The message to log.
   */
  void info(const std::string &message);

  /**
   * Log a debug message.
   * @param message The message to log.
   */
  void debug(const std::string &message);

private:
  Logger();
  void log(const std::string &level, const std::string &message);
  LOG_LEVEL current_level_;
  std::ostream *output_stream_;
  std::mutex mutex_;
};

} // namespace iqm

#define LOG_ERROR(msg) (iqm::Logger::get_instance().error(msg))
#define LOG_INFO(msg) (iqm::Logger::get_instance().info(msg))
#define LOG_DEBUG(msg) (iqm::Logger::get_instance().debug(msg))
