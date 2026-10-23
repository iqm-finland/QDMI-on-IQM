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
  set(QDMI_MINIMUM_VERSION 1.4.0
      CACHE STRING "Minimum QDMI version")
  set(QDMI_VERSION 1.4.0
      CACHE STRING "QDMI version")
  set(QDMI_REV "6db03d83ad602aaa9fe928b5c3fc5cce84b10061" # v1.4.0
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
include(${CMAKE_CURRENT_LIST_DIR}/BuildOpenSSL.cmake)
set(CPR_BUILD_TESTS
    OFF
    CACHE BOOL "Disable CPR tests" FORCE)
# CPR and curl are private dependencies; do not install their public interfaces.
set(CPR_ENABLE_INSTALL OFF)
set(CPR_USE_SYSTEM_CURL OFF)
set(CPR_FORCE_OPENSSL_BACKEND ON)
set(CURL_DISABLE_INSTALL ON)
set(USE_APPLE_SECTRUST ${APPLE})
set(CURL_USE_LIBSSH2 OFF)
set(USE_LIBIDN2 OFF)
set(CPR_CURL_USE_LIBPSL
    OFF
    CACHE BOOL "Disable libpsl for CPR curl" FORCE)
# Build curl's response decoders as private static dependencies.
set(CURL_ZLIB
    ON
    CACHE STRING "Enable zlib in bundled curl" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/BuildZlib.cmake)
set(BUILD_SHARED_LIBS OFF)
set(BROTLI_BUILD_TOOLS
    OFF
    CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_COMPRESSION OFF)
set(ZSTD_BUILD_DICTBUILDER OFF)
FetchContent_Declare(
  brotli
  URL https://github.com/google/brotli/archive/refs/tags/v1.2.1.tar.gz
  URL_HASH
    SHA256=98d35d11a78dc453aebb7d10c3748f3f73ff78c8267c0dc697b8631f67bf7e8f)
FetchContent_Declare(
  zstd
  URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
  URL_HASH
    SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
  SOURCE_SUBDIR build/cmake)
FetchContent_MakeAvailable(brotli zstd)
set_property(DIRECTORY "${brotli_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(DIRECTORY "${zstd_SOURCE_DIR}/build/cmake"
             PROPERTY EXCLUDE_FROM_ALL TRUE)
set(BROTLI_INCLUDE_DIR "${brotli_SOURCE_DIR}/c/include")
set(BROTLICOMMON_LIBRARY brotlicommon)
set(BROTLIDEC_LIBRARY brotlidec)
set(ZSTD_INCLUDE_DIR "${zstd_SOURCE_DIR}/lib")
set(ZSTD_LIBRARY libzstd_static)
set(CURL_BROTLI ON)
set(CURL_ZSTD ON)
# HTTP/1.1 needs no nghttp2 and avoids CVE-2026-92392 in cpr's bundled curl.
set(USE_NGHTTP2
    OFF
    CACHE BOOL "Disable HTTP/2 in bundled curl" FORCE)
FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.15.1)
list(APPEND FETCH_PACKAGES cpr)

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

# CPR uses BUILD_SHARED_LIBS to select its library type.
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(${FETCH_PACKAGES})
