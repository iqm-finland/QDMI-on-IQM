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

# Collect source files and header files for the internal library
file(GLOB INTERNAL_SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE INTERNAL_HEADER_FILES "${PROJECT_SOURCE_DIR}/include/*.hpp")

set(QDMI_INTERNAL_TARGET_NAME iqm_qdmi_device_internals)
add_library(${QDMI_INTERNAL_TARGET_NAME} STATIC)
add_library(iqm::qdmi_device_internals ALIAS ${QDMI_INTERNAL_TARGET_NAME})
target_compile_features(${QDMI_INTERNAL_TARGET_NAME} PRIVATE cxx_std_20)
set_target_properties(
  ${QDMI_INTERNAL_TARGET_NAME}
  PROPERTIES POSITION_INDEPENDENT_CODE ON
             C_VISIBILITY_PRESET hidden
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN 1)
target_sources(${QDMI_INTERNAL_TARGET_NAME} PRIVATE ${INTERNAL_SRC_FILES})
target_sources(
  ${QDMI_INTERNAL_TARGET_NAME}
  PUBLIC FILE_SET
         public_headers
         TYPE
         HEADERS
         BASE_DIRS
         ${PROJECT_SOURCE_DIR}/include
         ${CMAKE_CURRENT_BINARY_DIR}/../include
         FILES
         ${INTERNAL_HEADER_FILES}
         ${PUBLIC_HEADER_FILES})
target_link_libraries(
  ${QDMI_INTERNAL_TARGET_NAME}
  PRIVATE qdmi::qdmi_project_warnings CURL::libcurl
          nlohmann_json::nlohmann_json)

if(ENABLE_COVERAGE)
  if(TARGET qdmi::qdmi_coverage_flags)
    target_link_libraries(${QDMI_INTERNAL_TARGET_NAME}
                          PUBLIC qdmi::qdmi_coverage_flags)
  endif()
endif()
