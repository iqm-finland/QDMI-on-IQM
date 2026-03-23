# Copyright (c) 2025 - 2026 IQM QDMI developers
# All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_executable(unit_tests test_iqm_api_config.cpp test_iqm_auth.cpp
                          test_iqm_device.cpp test_logging.cpp)
target_compile_features(unit_tests PRIVATE cxx_std_20)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(
  unit_tests
  PRIVATE gtest_main
          gmock_main
          gtest
          gmock
          qdmi::qdmi
          qdmi::qdmi_project_warnings
          ${QDMI_TARGET_NAME}
          mocks)
gtest_discover_tests(unit_tests)
