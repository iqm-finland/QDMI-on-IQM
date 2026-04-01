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
  ${CMAKE_SOURCE_DIR}/src/curl_http_client.cpp
  ${CMAKE_SOURCE_DIR}/src/iqm_api_config.cpp
  ${CMAKE_SOURCE_DIR}/src/iqm_auth.cpp
  ${CMAKE_SOURCE_DIR}/src/iqm_device.cpp
  ${CMAKE_SOURCE_DIR}/src/logging.cpp
  test_iqm_api_config.cpp
  test_iqm_auth.cpp
  test_iqm_device.cpp
  test_logging.cpp)
target_compile_features(unit_tests PRIVATE cxx_std_20)
target_compile_definitions(
  unit_tests
  PRIVATE QDMI_VERSION="${QDMI_VERSION}"
          IQM_QDMI_DEVICE_VERSION="${CMAKE_PROJECT_VERSION}"
          IQM_QDMI_device_EXPORTS)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include
                                              ${CMAKE_BINARY_DIR}/src/include)
target_link_libraries(
  unit_tests
  PRIVATE gtest_main
          gmock_main
          gtest
          gmock
          qdmi::qdmi_project_warnings
          CURL::libcurl
          nlohmann_json::nlohmann_json
          mocks)
gtest_discover_tests(unit_tests)
