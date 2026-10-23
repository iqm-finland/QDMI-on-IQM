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
  message(
    FATAL_ERROR
      "The device requires its private OpenSSL build; configure it before other OpenSSL consumers."
  )
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

set(_openssl_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID)
  set(_openssl_arch "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _openssl_arch_count)
  if(NOT _openssl_arch_count EQUAL 1)
    message(
      FATAL_ERROR "Build one macOS architecture at a time for bundled OpenSSL.")
  endif()
  set(_openssl_arch "${CMAKE_OSX_ARCHITECTURES}")
endif()
string(TOLOWER "${_openssl_arch}" _openssl_arch)
if(_openssl_arch MATCHES "^(aarch64|arm64)$")
  set(_openssl_arch arm64)
elseif(_openssl_arch MATCHES "^(amd64|x86_64|x64)$")
  set(_openssl_arch x86_64)
else()
  message(
    FATAL_ERROR
      "Bundled OpenSSL does not support ${CMAKE_SYSTEM_NAME}/${_openssl_arch}.")
endif()

# Static OpenSSL uses /Zl on MSVC and can serve Debug and Release device builds.
set(_openssl_options --release no-shared no-module no-apps --libdir=lib)
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL}
   AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
  set(_openssl_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
  ProcessorCount(_openssl_jobs)
  if(NOT _openssl_jobs)
    set(_openssl_jobs 1)
  endif()
endif()

if(MSVC)
  if(_openssl_arch STREQUAL arm64)
    set(_openssl_target VC-WIN64-ARM)
    if(CMAKE_C_COMPILER_ID STREQUAL Clang)
      set(_openssl_target VC-CLANG-WIN64-CLANGASM-ARM)
    endif()
  else()
    set(_openssl_target VC-WIN64A)
    find_program(_openssl_nasm NAMES nasm REQUIRED)
  endif()
  get_filename_component(_openssl_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
  get_filename_component(_openssl_compiler "${CMAKE_C_COMPILER}" NAME)
  list(APPEND _openssl_options "CC=${_openssl_compiler}")
  find_program(_openssl_jom NAMES jom)
  if(_openssl_jom)
    set(_openssl_make "${_openssl_jom}" /J ${_openssl_jobs})
    list(APPEND _openssl_options /FS)
  else()
    find_program(
      _openssl_nmake
      NAMES nmake
      HINTS "${_openssl_compiler_dir}")
    set(_openssl_make nmake /NOLOGO)
  endif()

  # Visual Studio generators need the same developer environment as Ninja.
  string(REGEX REPLACE ",.*$" "" _openssl_vs "${CMAKE_GENERATOR_INSTANCE}")
  if(NOT _openssl_vs AND _openssl_compiler_dir MATCHES
                         "^(.*)/VC/Tools/MSVC/[^/]+/bin/Host[^/]+/[^/]+$")
    set(_openssl_vs "${CMAKE_MATCH_1}")
  endif()
  find_file(
    _openssl_vcvarsall vcvarsall.bat
    HINTS "${_openssl_vs}/VC/Auxiliary/Build"
          "$ENV{VCINSTALLDIR}/Auxiliary/Build"
    NO_DEFAULT_PATH)
  if(_openssl_vcvarsall)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
      set(_openssl_host arm64)
    else()
      set(_openssl_host amd64)
    endif()
    if(_openssl_arch STREQUAL arm64)
      set(_openssl_windows_arch arm64)
    else()
      set(_openssl_windows_arch amd64)
    endif()
    set(_openssl_vcvars_arch "${_openssl_host}")
    if(NOT _openssl_host STREQUAL _openssl_windows_arch)
      string(APPEND _openssl_vcvars_arch "_${_openssl_windows_arch}")
    endif()
    set(_openssl_environment
        "call \"${_openssl_vcvarsall}\" ${_openssl_vcvars_arch} >nul\nif errorlevel 1 exit /b 1\n"
    )
  elseif(NOT _openssl_nmake AND NOT _openssl_jom)
    message(
      FATAL_ERROR
        "Building OpenSSL requires a Visual Studio developer environment with nmake or jom."
    )
  endif()
  string(APPEND _openssl_environment
         "set \"PATH=${_openssl_compiler_dir};%PATH%\"\n")
elseif(APPLE OR CMAKE_SYSTEM_NAME STREQUAL Linux)
  find_program(_openssl_make_program NAMES gmake make REQUIRED)
  set(_openssl_make "${_openssl_make_program}" -j${_openssl_jobs})
  list(APPEND _openssl_options "CC=\"${CMAKE_C_COMPILER}\"" -fPIC)
  if(APPLE)
    set(_openssl_target "darwin64-${_openssl_arch}-cc")
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
  else()
    if(CMAKE_CROSSCOMPILING)
      message(FATAL_ERROR "Bundled OpenSSL requires a native Linux toolchain.")
    endif()
    if(_openssl_arch STREQUAL arm64)
      set(_openssl_target linux-aarch64)
    else()
      set(_openssl_target linux-x86_64)
    endif()
    if(CMAKE_SYSROOT)
      list(APPEND _openssl_options "--sysroot=${CMAKE_SYSROOT}")
    endif()
  endif()
else()
  message(FATAL_ERROR "Bundled OpenSSL does not support ${CMAKE_SYSTEM_NAME}.")
endif()

# A different toolchain or target configuration gets a separate private prefix.
string(
  SHA256
    _openssl_signature
    "${OPENSSL_VERSION};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_VERSION};${_openssl_target};${_openssl_options};${_openssl_environment}"
)
string(SUBSTRING "${_openssl_signature}" 0 16 _openssl_signature)
set(_openssl_build "${openssl_BINARY_DIR}/${_openssl_signature}")
set(OPENSSL_ROOT_DIR "${_openssl_build}/install")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_SSL_LIBRARY
    "${OPENSSL_ROOT_DIR}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(OPENSSL_CRYPTO_LIBRARY
    "${OPENSSL_ROOT_DIR}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(NOT EXISTS "${_openssl_build}/complete"
   OR NOT EXISTS "${OPENSSL_SSL_LIBRARY}"
   OR NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}"
   OR NOT EXISTS "${OPENSSL_INCLUDE_DIR}/openssl/opensslconf.h")
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
      "${_openssl_target}"
      ${_openssl_options}
      "--prefix=${OPENSSL_ROOT_DIR}")
  message(
    STATUS "Building static OpenSSL ${OPENSSL_VERSION} (${_openssl_target})")
  if(MSVC)
    foreach(_openssl_command IN ITEMS _openssl_configure _openssl_make)
      list(TRANSFORM ${_openssl_command} PREPEND "\"")
      list(TRANSFORM ${_openssl_command} APPEND "\"")
      list(JOIN ${_openssl_command} " " ${_openssl_command})
    endforeach()
    file(
      WRITE "${_openssl_build}/build.cmd"
      "@echo off\n${_openssl_environment}${_openssl_configure}\nif errorlevel 1 exit /b 1\n${_openssl_make} install_dev\n"
    )
    execute_process(
      COMMAND cmd /c "${_openssl_build}/build.cmd"
      WORKING_DIRECTORY "${_openssl_build}"
      RESULT_VARIABLE _openssl_result
      OUTPUT_FILE "${_openssl_build}/build.log"
      ERROR_VARIABLE _openssl_error)
  else()
    execute_process(
      COMMAND ${_openssl_configure}
      WORKING_DIRECTORY "${_openssl_build}"
      RESULT_VARIABLE _openssl_result
      OUTPUT_FILE "${_openssl_build}/configure.log"
      ERROR_VARIABLE _openssl_error)
    if(_openssl_result EQUAL 0)
      execute_process(
        COMMAND ${_openssl_make} install_dev
        WORKING_DIRECTORY "${_openssl_build}"
        RESULT_VARIABLE _openssl_result
        OUTPUT_FILE "${_openssl_build}/build.log"
        ERROR_VARIABLE _openssl_error)
    endif()
  endif()
  if(NOT _openssl_result EQUAL 0)
    message(
      FATAL_ERROR
        "OpenSSL build failed (${_openssl_result}); see ${_openssl_build}/*.log\n${_openssl_error}"
    )
  endif()
  file(WRITE "${_openssl_build}/complete" "${_openssl_signature}\n")
endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
# Use the dependency metadata from this build, never a host pkg-config file.
find_package(
  OpenSSL
  ${OPENSSL_VERSION}
  EXACT
  REQUIRED
  CONFIG
  PATHS
  "${OPENSSL_ROOT_DIR}/lib/cmake/OpenSSL"
  NO_DEFAULT_PATH)
