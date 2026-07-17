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

include(FetchContent)
set(FETCH_PACKAGES "")

if(TARGET qdmi::qdmi)
  message(STATUS "QDMI is already available.")
else()
  message(STATUS "QDMI will be included via FetchContent")
  # cmake-format: off
  set(QDMI_MINIMUM_VERSION 1.3.0
      CACHE STRING "Minimum QDMI version")
  set(QDMI_VERSION 1.3.2
      CACHE STRING "QDMI version")
  set(QDMI_REV "d05a0b418f42e54e9585d2e00af8ce23e745fd83" # v1.3.2
      CACHE STRING "QDMI identifier (tag, branch or commit hash)")
  set(QDMI_REPO_OWNER "Munich-Quantum-Software-Stack"
      CACHE STRING "QDMI repository owner (change when using a fork)")
  set(INSTALL_QDMI
      OFF
      CACHE BOOL "Generate installation instructions for QDMI")
  # cmake-format: on
  FetchContent_Declare(
    qdmi
    GIT_REPOSITORY https://github.com/${QDMI_REPO_OWNER}/qdmi.git
    GIT_TAG ${QDMI_REV}
    FIND_PACKAGE_ARGS ${QDMI_MINIMUM_VERSION})
  list(APPEND FETCH_PACKAGES qdmi)
endif()

set(JSON_VERSION
    3.12.0
    CACHE STRING "nlohmann_json version")
set(JSON_URL
    https://github.com/nlohmann/json/releases/download/v${JSON_VERSION}/json.tar.xz
)
set(JSON_SystemInclude
    ON
    CACHE INTERNAL "Treat the library headers like system headers")
FetchContent_Declare(nlohmann_json URL ${JSON_URL} FIND_PACKAGE_ARGS
                                       ${JSON_VERSION})
list(APPEND FETCH_PACKAGES nlohmann_json)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CPR_BUILD_TESTS
    OFF
    CACHE BOOL "Disable CPR tests" FORCE)
set(CPR_CURL_USE_LIBPSL
    OFF
    CACHE BOOL "Disable libpsl for CPR curl" FORCE)
set(CPR_USE_SYSTEM_CURL
    OFF
    CACHE BOOL "Use system curl for CPR")
set(BUILD_STATIC_CURL
    ON
    CACHE BOOL "Enable static curl for CPR" FORCE)
set(BUILD_SHARED_LIBS
    OFF
    CACHE BOOL "Disable shared libraries for CPR" FORCE)
FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.14.2
  FIND_PACKAGE_ARGS 1.14.2)
list(APPEND FETCH_PACKAGES cpr)

if(BUILD_IQM_SPANK)
  find_path(
    SLURM_SPANK_INCLUDE_DIR
    NAMES spank.h
    PATH_SUFFIXES slurm
    HINTS /opt/slurm/include
    DOC "Path to Slurm SPANK headers")

  if(NOT SLURM_SPANK_INCLUDE_DIR)
    message(
      FATAL_ERROR
        "BUILD_IQM_SPANK is ON but Slurm SPANK headers were not "
        "found. Please install Slurm development headers "
        "(e.g. slurm-devel or libslurm-dev), or provide "
        "SLURM_SPANK_INCLUDE_DIR.")
  endif()

  mark_as_advanced(SLURM_SPANK_INCLUDE_DIR)
endif()

if(BUILD_IQM_QDMI_TESTS)
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
  set(GTEST_VERSION
      1.17.0
      CACHE STRING "Google Test version")
  set(GTEST_URL
      https://github.com/google/googletest/archive/refs/tags/v${GTEST_VERSION}.tar.gz
  )
  FetchContent_Declare(googletest URL ${GTEST_URL} FIND_PACKAGE_ARGS
                                      ${GTEST_VERSION} NAMES GTest)
  list(APPEND FETCH_PACKAGES googletest)
endif()

if(BUILD_IQM_QDMI_DOCS)
  set(CMAKE_POLICY_DEFAULT_CMP0116
      NEW
      CACHE STRING
            "Set the default CMP0116 policy to NEW for documentation builds")
  set(DOXYGEN_MIN_VERSION
      1.15.0
      CACHE STRING "Doxygen version")
  set(DOXYGEN_REV
      "669aeeefca743c148e2d935b3d3c69535c7491e6" # v1.16.1
      CACHE STRING "Doxygen identifier (tag, branch or commit hash)")
  FetchContent_Declare(
    Doxygen
    GIT_REPOSITORY https://github.com/doxygen/doxygen.git
    GIT_TAG ${DOXYGEN_REV}
    FIND_PACKAGE_ARGS ${DOXYGEN_MIN_VERSION})
  list(APPEND FETCH_PACKAGES Doxygen)
endif()

# Make all declared dependencies available.
FetchContent_MakeAvailable(${FETCH_PACKAGES})
