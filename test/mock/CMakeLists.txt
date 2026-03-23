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

add_library(mocks INTERFACE)

# Define the mock headers using file set
set(MOCK_HEADERS mock_http_client.hpp)

target_sources(
  mocks INTERFACE FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
                  ${MOCK_HEADERS})

# Add include directories for dependencies
target_include_directories(mocks INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(mocks INTERFACE gmock gtest)
target_compile_features(mocks INTERFACE cxx_std_20)
