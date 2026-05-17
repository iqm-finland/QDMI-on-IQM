# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_executable(
  unit_tests
  curl_http_client_test_support.cpp test_curl_http_client.cpp
  test_iqm_api_config.cpp test_iqm_auth.cpp test_iqm_device.cpp
  test_logging.cpp)
target_compile_features(unit_tests PRIVATE cxx_std_20)
target_link_libraries(
  unit_tests
  PRIVATE GTest::gmock_main
          GTest::gtest
          qdmi::qdmi_project_warnings
          ${QDMI_TARGET_NAME}
          iqm::qdmi_device_internals
          mocks
          CURL::libcurl)

if(WIN32)
  add_custom_command(
    TARGET unit_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${QDMI_TARGET_NAME}> $<TARGET_FILE_DIR:unit_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:CURL::libcurl>
            $<TARGET_FILE_DIR:unit_tests>)
endif()

gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 60)
