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

#include "logging.hpp"

#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>

// NOTE: The logger is a singleton, and its log level is set on the first call
// to get_instance() based on the IQM_CPP_API_LOG_LEVEL environment variable.
// Because gtest runs all tests in a single process, it's not possible to test
// different log levels by setting the environment variable in different tests,
// as the logger will only be initialized once.
//
// This test checks the default behavior, where the log level is ERROR.
// To test other log levels, you would need to run tests in separate processes,
// for example by using ctest with the --repeat-until-fail option and setting
// the environment variable before running the test executable.

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
