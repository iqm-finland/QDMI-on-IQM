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
set(BUILD_SHARED_LIBS OFF)
if(APPLE)
  if(NOT TARGET CURL::libcurl)
    # Use Apple's SDK curl, even when Homebrew curl is installed.
    set(_curl_sdk "${CMAKE_OSX_SYSROOT}")
    if(NOT _curl_sdk)
      set(_curl_sdk macosx)
    endif()
    if(NOT IS_DIRECTORY "${_curl_sdk}")
      execute_process(
        COMMAND xcrun --sdk "${_curl_sdk}" --show-sdk-path
        OUTPUT_VARIABLE _curl_sdk
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endif()
    add_library(CURL::libcurl UNKNOWN IMPORTED)
    set_target_properties(
      CURL::libcurl
      PROPERTIES IMPORTED_LOCATION "${_curl_sdk}/usr/lib/libcurl.tbd"
                 INTERFACE_INCLUDE_DIRECTORIES "${_curl_sdk}/usr/include")
  endif()
else()
  # Build only curl; TLS and response codecs come from the host.
  if(UNIX)
    find_package(OpenSSL 3 REQUIRED)
  endif()
  set(BUILD_CURL_EXE OFF)
  set(BUILD_TESTING OFF)
  set(CURL_DISABLE_INSTALL ON)
  foreach(
    PROTOCOL
    DICT
    FILE
    FTP
    GOPHER
    IMAP
    IPFS
    LDAP
    LDAPS
    MQTT
    POP3
    RTSP
    SMTP
    TELNET
    TFTP
    WEBSOCKETS)
    set(CURL_DISABLE_${PROTOCOL} ON)
  endforeach()
  set(CURL_USE_OPENSSL ${UNIX})
  set(CURL_USE_SCHANNEL ${WIN32})
  set(CURL_USE_LIBSSH2 OFF)
  set(USE_LIBIDN2 OFF)
  set(CURL_USE_LIBPSL OFF)
  FetchContent_Declare(
    curl
    URL https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
    URL_HASH
      SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5)
  list(APPEND FETCH_PACKAGES curl)
endif()

set(CPR_BUILD_TESTS
    OFF
    CACHE BOOL "Disable CPR tests" FORCE)
set(CPR_ENABLE_INSTALL OFF)
# Configuring curl directly avoids CPR's bundled zlib build.
set(CPR_USE_EXISTING_CURL_TARGET ON)
set(CPR_FORCE_OPENSSL_BACKEND OFF)
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

FetchContent_MakeAvailable(${FETCH_PACKAGES})
