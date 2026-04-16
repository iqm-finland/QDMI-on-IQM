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

include(FetchContent)
include(CMakeDependentOption)
set(FETCH_PACKAGES "")

if(TARGET qdmi::qdmi)
  message(STATUS "QDMI is already available.")
else()
  message(STATUS "QDMI will be included via FetchContent")
  # cmake-format: off
  set(QDMI_VERSION 1.2.2
      CACHE STRING "QDMI version")
  set(QDMI_REV "c5f71c63d5bf3b97c26c60efd367a4be3670d3a0" # v1.2.x
      CACHE STRING "QDMI identifier (tag, branch or commit hash)")
  set(QDMI_REPO_OWNER "Munich-Quantum-Software-Stack"
      CACHE STRING "QDMI repository owner (change when using a fork)")
  set(QDMI_INSTALL
      OFF
      CACHE BOOL "Generate installation instructions for QDMI")
  # cmake-format: on
  FetchContent_Declare(
    qdmi
    GIT_REPOSITORY https://github.com/${QDMI_REPO_OWNER}/qdmi.git
    GIT_TAG ${QDMI_REV}
    FIND_PACKAGE_ARGS ${QDMI_VERSION})
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

if(WIN32)
  set(CURL_REV
      "curl-8_19_0"
      CACHE STRING "curl identifier (tag, branch or commit hash)")
  set(BUILD_CURL_EXE
      OFF
      CACHE BOOL "Disable curl executable for vendored builds" FORCE)
  set(BUILD_LIBCURL_DOCS
      OFF
      CACHE BOOL "Disable libcurl docs for vendored builds" FORCE)
  set(BUILD_MISC_DOCS
      OFF
      CACHE BOOL "Disable miscellaneous curl docs for vendored builds" FORCE)
  set(CURL_USE_SCHANNEL
      ON
      CACHE BOOL "Use Schannel backend for vendored Windows curl" FORCE)
  set(CURL_USE_OPENSSL
      OFF
      CACHE BOOL "Disable OpenSSL backend for vendored Windows curl" FORCE)
  set(CURL_USE_LIBPSL
      OFF
      CACHE BOOL "Disable libpsl for vendored Windows curl" FORCE)
  set(USE_NGHTTP2
      OFF
      CACHE BOOL "Disable nghttp2 for vendored Windows curl" FORCE)
  set(USE_LIBIDN2
      OFF
      CACHE BOOL "Disable libidn2 for vendored Windows curl" FORCE)
  set(CURL_ZLIB
      OFF
      CACHE BOOL "Disable zlib for vendored Windows curl" FORCE)
  set(CURL_BROTLI
      OFF
      CACHE BOOL "Disable brotli for vendored Windows curl" FORCE)
  set(CURL_ZSTD
      OFF
      CACHE BOOL "Disable zstd for vendored Windows curl" FORCE)
  set(CURL_DISABLE_LDAP
      ON
      CACHE BOOL "Disable LDAP for vendored Windows curl" FORCE)
  set(CURL_DISABLE_LDAPS
      ON
      CACHE BOOL "Disable LDAPS for vendored Windows curl" FORCE)

  FetchContent_Declare(
    CURL
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG ${CURL_REV})

  # curl's build system defines BUILD_SHARED_LIBS; keep that setting local to
  # the curl subproject so it does not affect other dependencies.
  set(_iqm_build_shared_libs_was_defined OFF)
  if(DEFINED BUILD_SHARED_LIBS)
    set(_iqm_build_shared_libs_was_defined ON)
    set(_iqm_build_shared_libs_previous_value "${BUILD_SHARED_LIBS}")
  endif()

  set(BUILD_SHARED_LIBS
      ON
      CACHE BOOL "Build shared libraries" FORCE)
  FetchContent_MakeAvailable(CURL)

  if(_iqm_build_shared_libs_was_defined)
    set(BUILD_SHARED_LIBS
        "${_iqm_build_shared_libs_previous_value}"
        CACHE BOOL "Build shared libraries" FORCE)
  else()
    unset(BUILD_SHARED_LIBS CACHE)
    unset(BUILD_SHARED_LIBS)
  endif()
else()
  find_package(CURL REQUIRED)
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
  set(DOXYGEN_VERSION
      1.16.1
      CACHE STRING "Doxygen version")
  set(DOXYGEN_REV
      "669aeeefca743c148e2d935b3d3c69535c7491e6"
      CACHE STRING "Doxygen identifier (tag, branch or commit hash)")
  FetchContent_Declare(
    Doxygen
    GIT_REPOSITORY https://github.com/doxygen/doxygen.git
    GIT_TAG ${DOXYGEN_REV}
    FIND_PACKAGE_ARGS ${DOXYGEN_VERSION})
  list(APPEND FETCH_PACKAGES Doxygen)

  set(DOXYGEN_AWESOME_VERSION
      2.4.2
      CACHE STRING "Doxygen Awesome version")
  set(DOXYGEN_AWESOME_REV
      "d52eafe3e9303399fda15661f3d7bb8fe3d7eabc"
      CACHE STRING "Doxygen Awesome identifier (tag, branch or commit hash)")
  FetchContent_Declare(
    doxygen-awesome-css
    GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
    GIT_TAG ${DOXYGEN_AWESOME_REV}
    FIND_PACKAGE_ARGS ${DOXYGEN_AWESOME_VERSION})
  list(APPEND FETCH_PACKAGES doxygen-awesome-css)
endif()

# Make all declared dependencies available.
FetchContent_MakeAvailable(${FETCH_PACKAGES})
