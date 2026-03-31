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

add_executable(integration_tests test_iqm_device_integration.cpp)
target_compile_features(integration_tests PRIVATE cxx_std_20)
target_include_directories(
  integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/include
                            ${CMAKE_SOURCE_DIR}/test/fomac)
target_link_libraries(
  integration_tests
  PRIVATE gtest_main gtest qdmi::qdmi qdmi::qdmi_project_warnings
          ${QDMI_TARGET_NAME} qdmi::iqm_fomac)
add_dependencies(integration_tests qdmi_test_iqm_device_defs)
gtest_discover_tests(
  integration_tests
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
             DISCOVERY_TIMEOUT 60)
