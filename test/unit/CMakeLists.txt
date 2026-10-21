# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
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
  ../qdmi_client/qdmi_client.cpp
  http_stub.cpp
  test_http_client.cpp
  test_iqm_api_config.cpp
  test_iqm_auth.cpp
  test_iqm_device.cpp
  test_logging.cpp)
target_include_directories(unit_tests PRIVATE ../qdmi_client)
target_compile_features(unit_tests PRIVATE cxx_std_20)
target_link_libraries(
  unit_tests PRIVATE GTest::gtest_main GTest::gtest qdmi::qdmi_project_warnings
                     iqm::qdmi_device_internals nlohmann_json::nlohmann_json)

gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 60)

find_package(
  Python
  COMPONENTS Interpreter
  QUIET)
if(Python_Interpreter_FOUND)
  add_executable(tls_probe tls_probe.cpp)
  target_compile_features(tls_probe PRIVATE cxx_std_20)
  target_link_libraries(tls_probe PRIVATE iqm::qdmi_device_internals
                                          qdmi::qdmi_project_warnings)
  add_test(
    NAME Transport.CaBundle
    COMMAND
      ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_tls_bundle.py
      $<TARGET_FILE:tls_probe>)
  set_tests_properties(Transport.CaBundle PROPERTIES SKIP_RETURN_CODE 77)
endif()
