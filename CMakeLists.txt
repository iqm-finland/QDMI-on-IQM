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

cmake_minimum_required(VERSION 3.24...4.2)
project(
  iqm-qdmi-device
  LANGUAGES C CXX
  VERSION 1.0.0
  DESCRIPTION "Implementation of a QDMI device for IQM Quantum Computers")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                               "MinSizeRel" "RelWithDebInfo")
endif()

option(USE_INSTALLED_IQM_QDMI_DEVICE
       "Use an installed version of the IQM QDMI Device to build tests" OFF)

if(USE_INSTALLED_IQM_QDMI_DEVICE)
  find_package(iqm-qdmi-device REQUIRED ${PROJECT_VERSION})
endif()

# Generate compile_commands.json to make it easier to work with clang based
# tools
set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE BOOL "Export compile commands" FORCE)

set(CMAKE_VERIFY_INTERFACE_HEADER_SETS
    ON
    CACHE BOOL "Verify interface header sets" FORCE)

set(QDMI_PREFIX "IQM")

# check if this is the master project or used via add_subdirectory
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(IQM_QDMI_MASTER_PROJECT ON)
else()
  set(IQM_QDMI_MASTER_PROJECT OFF)
endif()

option(BUILD_IQM_QDMI_TESTS "Build tests for the IQM QDMI device"
       ${IQM_QDMI_MASTER_PROJECT})
option(BUILD_IQM_QDMI_DOCS "Build documentation for the IQM QDMI device" OFF)

# Manage external dependencies
include(cmake/ExternalDependencies.cmake)

# Set target name
set(QDMI_TARGET_NAME "iqm-qdmi-device")

# Add the main library code
if(NOT USE_INSTALLED_IQM_QDMI_DEVICE)
  add_subdirectory(src)
endif()

# Add the documentation
if(BUILD_IQM_QDMI_DOCS)
  add_subdirectory(docs)
endif()

# Add the tests
if(BUILD_IQM_QDMI_TESTS)
  enable_testing()
  include(GoogleTest)
  add_subdirectory(test)
endif()
