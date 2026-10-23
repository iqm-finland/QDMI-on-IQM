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

# curl links zlib during its OpenSSL probes, so install it before configuring
# CPR.
if(TARGET ZLIB::ZLIB)
  message(
    FATAL_ERROR
      "The device requires its private zlib build before other zlib consumers.")
endif()
# Download only: CPR reuses this declaration without adding a second zlib build.
FetchContent_Declare(
  zlib
  URL https://github.com/zlib-ng/zlib-ng/archive/2.3.3.tar.gz
  URL_HASH
    SHA256=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
  SOURCE_SUBDIR cmake-download-only)
FetchContent_MakeAvailable(zlib)

set(_zlib_build "${zlib_BINARY_DIR}/bootstrap")
set(_zlib_prefix "${_zlib_build}/install")
set(_zlib_options
    -G
    "${CMAKE_GENERATOR}"
    -DZLIB_COMPAT=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    "-DCMAKE_INSTALL_PREFIX=${_zlib_prefix}"
    -DCMAKE_INSTALL_LIBDIR=lib)
set(_zlib_build_type "${CMAKE_BUILD_TYPE}")
if(NOT _zlib_build_type)
  set(_zlib_build_type Release)
endif()
list(APPEND _zlib_options "-DCMAKE_BUILD_TYPE=${_zlib_build_type}")
set(_zlib_configurations ${CMAKE_CONFIGURATION_TYPES})
if(NOT _zlib_configurations)
  set(_zlib_configurations "${_zlib_build_type}")
endif()
set(_zlib_variables
    CMAKE_C_COMPILER
    CMAKE_MAKE_PROGRAM
    CMAKE_TOOLCHAIN_FILE
    CMAKE_GENERATOR_PLATFORM
    CMAKE_GENERATOR_TOOLSET
    CMAKE_GENERATOR_INSTANCE
    CMAKE_CONFIGURATION_TYPES
    CMAKE_C_FLAGS
    CMAKE_OSX_ARCHITECTURES
    CMAKE_OSX_DEPLOYMENT_TARGET
    CMAKE_OSX_SYSROOT
    CMAKE_SYSROOT)
foreach(_configuration IN LISTS _zlib_configurations)
  string(TOUPPER "${_configuration}" _configuration_upper)
  list(APPEND _zlib_variables "CMAKE_C_FLAGS_${_configuration_upper}")
  if(CMAKE_CONFIGURATION_TYPES)
    string(TOLOWER "${_configuration}" _configuration_lower)
    list(APPEND _zlib_options
         "-DCMAKE_${_configuration_upper}_POSTFIX=_${_configuration_lower}")
  endif()
endforeach()
foreach(_variable IN LISTS _zlib_variables)
  string(REPLACE ";" "\\;" _value "${${_variable}}")
  list(APPEND _zlib_options "-D${_variable}:STRING=${_value}")
endforeach()
if(MSVC)
  set(_zlib_runtime "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(_zlib_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
  list(APPEND _zlib_options "-DCMAKE_MSVC_RUNTIME_LIBRARY=${_zlib_runtime}")
endif()

function(_zlib_run stage)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_FILE "${_zlib_build}/${stage}.log"
    ERROR_VARIABLE _error)
  if(NOT _result STREQUAL "0")
    message(
      FATAL_ERROR
        "zlib-ng ${stage} failed; see ${_zlib_build}/${stage}.log\n${_error}")
  endif()
endfunction()

file(MAKE_DIRECTORY "${_zlib_build}")
_zlib_run(configure -S "${zlib_SOURCE_DIR}" -B "${_zlib_build}"
          "${_zlib_options}")
foreach(_configuration IN LISTS _zlib_configurations)
  _zlib_run("build-${_configuration}" --build "${_zlib_build}" --config
            "${_configuration}")
  _zlib_run("install-${_configuration}" --install "${_zlib_build}" --config
            "${_configuration}")
endforeach()
set(ZLIB_DIR "${_zlib_prefix}/lib/cmake/ZLIB")
find_package(ZLIB REQUIRED CONFIG PATHS "${ZLIB_DIR}" NO_DEFAULT_PATH)
# Keep curl's FindZLIB on the imported target and headers from this private
# build.
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIR "${_zlib_prefix}/include")
