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

if(NOT DEFINED SDK_LIBRARY OR NOT DEFINED SDK_OUTPUT)
  message(FATAL_ERROR "SDK_LIBRARY and SDK_OUTPUT are required")
endif()

# CMake uses the platform's native binary inspection tool. The build directory
# helps Windows find DLLs supplied by the build rather than the operating
# system.
set(search_directories "${SDK_BUILD_DIR}")
if(CMAKE_HOST_WIN32)
  file(GLOB_RECURSE built_dlls "${SDK_BUILD_DIR}/*.dll")
  foreach(dll IN LISTS built_dlls)
    get_filename_component(DIRECTORY "${dll}" DIRECTORY)
    list(APPEND search_directories "${directory}")
  endforeach()
  list(REMOVE_DUPLICATES search_directories)
endif()

file(
  GET_RUNTIME_DEPENDENCIES
  LIBRARIES
  "${SDK_LIBRARY}"
  RESOLVED_DEPENDENCIES_VAR
  resolved
  UNRESOLVED_DEPENDENCIES_VAR
  unresolved
  CONFLICTING_DEPENDENCIES_PREFIX
  conflicts
  DIRECTORIES
  ${search_directories}
  PRE_EXCLUDE_REGEXES
  "^api-ms-win-"
  "^ext-ms-")

if(unresolved)
  message(FATAL_ERROR "Unresolved SDK runtime dependencies: ${unresolved}")
endif()
if(conflicts_FILENAMES)
  message(
    FATAL_ERROR "Conflicting SDK runtime dependencies: ${conflicts_FILENAMES}")
endif()

file(WRITE "${SDK_OUTPUT}" "")
foreach(dependency IN LISTS resolved)
  file(APPEND "${SDK_OUTPUT}" "${dependency}\n")
endforeach()
