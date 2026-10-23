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

# curl probes OpenSSL while configuring, so build it before configuring CPR.
if(TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto)
  message(FATAL_ERROR "Configure the device before other OpenSSL consumers.")
endif()
if(CMAKE_CROSSCOMPILING
   OR (APPLE
       AND CMAKE_OSX_ARCHITECTURES
       AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_SYSTEM_PROCESSOR))
  message(
    FATAL_ERROR "Bundled OpenSSL requires a native, single-architecture build.")
endif()
include(FetchContent)
include(ProcessorCount)
find_package(Perl REQUIRED)
set(OPENSSL_VERSION 3.5.9)
FetchContent_Declare(
  openssl
  URL https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz
  URL_HASH
    SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(openssl)

ProcessorCount(_openssl_jobs)
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL}
   AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
  set(_openssl_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
elseif(NOT _openssl_jobs)
  set(_openssl_jobs 1)
endif()
set(_openssl_options --release no-shared no-module no-apps --libdir=lib)
if(MSVC)
  if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
    list(APPEND _openssl_options VC-WIN64-ARM)
  elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    list(APPEND _openssl_options VC-WIN64A)
    find_program(_openssl_nasm nasm REQUIRED)
  else()
    message(FATAL_ERROR "Bundled OpenSSL requires Windows x64 or ARM64.")
  endif()
  # Use a Developer Command Prompt; upstream installation requires cl's PDB.
  list(APPEND _openssl_options CC=cl)
  find_program(_openssl_make NAMES jom nmake REQUIRED)
  set(_openssl_install "${_openssl_make}" /NOLOGO)
  if(_openssl_make MATCHES "jom([.]exe)?$")
    list(APPEND _openssl_install /J ${_openssl_jobs})
    list(APPEND _openssl_options /FS)
  endif()
elseif(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_program(_openssl_make NAMES gmake make REQUIRED)
  set(_openssl_install "${_openssl_make}" -j${_openssl_jobs})
  list(APPEND _openssl_options "CC=\"${CMAKE_C_COMPILER}\"" -fPIC)
  if(APPLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
      list(APPEND _openssl_options
           "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(CMAKE_OSX_SYSROOT)
      set(_openssl_sysroot "${CMAKE_OSX_SYSROOT}")
      if(NOT IS_ABSOLUTE "${_openssl_sysroot}")
        execute_process(
          COMMAND xcrun --sdk "${_openssl_sysroot}" --show-sdk-path
          OUTPUT_VARIABLE _openssl_sysroot
          OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
      endif()
      list(APPEND _openssl_options -isysroot "${_openssl_sysroot}")
    endif()
  elseif(CMAKE_SYSROOT)
    list(APPEND _openssl_options "--sysroot=${CMAKE_SYSROOT}")
  endif()
else()
  message(FATAL_ERROR "Bundled OpenSSL does not support ${CMAKE_SYSTEM_NAME}.")
endif()
list(APPEND _openssl_install install_dev)

# Invalidate cached builds when the recipe or toolchain settings change.
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _openssl_recipe_hash)
string(
  SHA256
    _openssl_signature
    "${_openssl_recipe_hash};${OPENSSL_VERSION};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_VERSION};$ENV{VCToolsVersion};${_openssl_options}"
)
string(SUBSTRING "${_openssl_signature}" 0 16 _openssl_signature)
set(_openssl_build "${openssl_BINARY_DIR}/${_openssl_signature}")
set(OPENSSL_ROOT_DIR "${_openssl_build}/install")
if(NOT EXISTS "${_openssl_build}/complete")
  file(MAKE_DIRECTORY "${_openssl_build}")
  set(_openssl_configure
      "${CMAKE_COMMAND}"
      -E
      env
      --unset=CFLAGS
      --unset=CPPFLAGS
      --unset=LDFLAGS
      "${PERL_EXECUTABLE}"
      "${openssl_SOURCE_DIR}/Configure"
      ${_openssl_options}
      "--prefix=${OPENSSL_ROOT_DIR}")
  message(
    STATUS "Building static OpenSSL ${OPENSSL_VERSION} in ${_openssl_build}")
  foreach(_openssl_step IN ITEMS configure install)
    execute_process(
      COMMAND ${_openssl_${_openssl_step}}
      WORKING_DIRECTORY "${_openssl_build}"
      RESULT_VARIABLE _openssl_result
      OUTPUT_FILE "${_openssl_build}/${_openssl_step}.log"
      ERROR_VARIABLE _openssl_error)
    if(NOT _openssl_result STREQUAL "0")
      message(
        FATAL_ERROR
          "OpenSSL ${_openssl_step} failed (${_openssl_result}); see ${_openssl_build}/${_openssl_step}.log\n${_openssl_error}"
      )
    endif()
  endforeach()
  file(WRITE "${_openssl_build}/complete" "${_openssl_signature}\n")
endif()

# Use this installation's dependency metadata, never a host pkg-config file.
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OpenSSL_DIR "${OPENSSL_ROOT_DIR}/lib/cmake/OpenSSL")
find_package(
  OpenSSL
  ${OPENSSL_VERSION}
  EXACT
  REQUIRED
  CONFIG
  PATHS
  "${OpenSSL_DIR}"
  NO_DEFAULT_PATH)
