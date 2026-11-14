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

cmake_minimum_required(VERSION 3.24)
project(installed-consumer LANGUAGES C)

find_package(iqm-qdmi-device CONFIG REQUIRED PATHS "${CMAKE_PREFIX_PATH}"
             NO_DEFAULT_PATH)
add_executable(consumer main.c)
target_compile_features(consumer PRIVATE c_std_11)
target_link_libraries(consumer PRIVATE iqm-qdmi-device)

if(EXISTS "${CMAKE_PREFIX_PATH}/licenses")
  set(notices qdmi cpr nlohmann_json)
  if(NOT APPLE)
    list(APPEND notices curl)
  endif()
  if(UNIX AND NOT APPLE)
    list(APPEND notices openssl)
  endif()
  foreach(dependency IN LISTS notices)
    if(NOT IS_DIRECTORY "${CMAKE_PREFIX_PATH}/licenses/${dependency}")
      message(FATAL_ERROR "Missing SDK license notices for ${dependency}")
    endif()
  endforeach()
endif()

get_target_property(library iqm-qdmi-device IMPORTED_LOCATION_RELEASE)
get_filename_component(library_dir "${library}" DIRECTORY)
if(NOT EXISTS "${library_dir}/iqm-qdmi-device.qdmi.json")
  message(FATAL_ERROR "Missing installed device manifest")
endif()

if(WIN32)
  get_target_property(runtime_files iqm-qdmi-device QDMI_RUNTIME_FILES)
  if(runtime_files)
    list(TRANSFORM runtime_files PREPEND "${library_dir}/")
  else()
    set(runtime_files "")
  endif()
  add_custom_command(
    TARGET consumer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${library}" ${runtime_files}
            $<TARGET_FILE_DIR:consumer>
    VERBATIM)
endif()
enable_testing()
add_test(NAME installed-consumer COMMAND consumer)
