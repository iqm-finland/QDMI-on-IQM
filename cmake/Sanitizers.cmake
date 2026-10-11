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

# Runtime sanitizer support. Enable with, for example,
# -DIQM_QDMI_SANITIZERS="address;undefined" on a Debug build.
#
# The flags are applied globally, before the external dependencies are declared,
# so that the dependencies are instrumented too. That matters for
# AddressSanitizer: an allocation made in uninstrumented code and freed in
# instrumented code (or the reverse) is a common source of false reports.

set(IQM_QDMI_SANITIZERS
    ""
    CACHE STRING
          "Semicolon-separated runtime sanitizers to build with. Supported \
values are 'address', 'undefined', 'thread', and 'memory'. Empty disables \
sanitizers.")

option(
  IQM_QDMI_SANITIZER_HALT_ON_ERROR
  "Make sanitizer diagnostics abort the process instead of only printing. \
Without this, an UndefinedBehaviorSanitizer finding leaves the test suite green."
  ON)

# Apply the requested sanitizers to every target defined from here on.
function(iqm_qdmi_apply_sanitizers)
  if(NOT IQM_QDMI_SANITIZERS)
    return()
  endif()

  set(supported_sanitizers address undefined thread memory)
  foreach(sanitizer IN LISTS IQM_QDMI_SANITIZERS)
    if(NOT sanitizer IN_LIST supported_sanitizers)
      message(
        FATAL_ERROR
          "Unknown sanitizer '${sanitizer}'. Supported values are: ${supported_sanitizers}"
      )
    endif()
  endforeach()

  # ThreadSanitizer and MemorySanitizer both shadow the whole address space and
  # cannot coexist with AddressSanitizer in one binary.
  if("thread" IN_LIST IQM_QDMI_SANITIZERS AND "address" IN_LIST
                                              IQM_QDMI_SANITIZERS)
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with "
                        "AddressSanitizer; configure them in separate builds")
  endif()
  if("memory" IN_LIST IQM_QDMI_SANITIZERS AND "address" IN_LIST
                                              IQM_QDMI_SANITIZERS)
    message(FATAL_ERROR "MemorySanitizer cannot be combined with "
                        "AddressSanitizer; configure them in separate builds")
  endif()

  if(MSVC)
    # MSVC only ships AddressSanitizer, spelled with its own flag, and it is
    # incompatible with the runtime checks that the Debug configuration enables
    # by default.
    foreach(sanitizer IN LISTS IQM_QDMI_SANITIZERS)
      if(NOT sanitizer STREQUAL "address")
        message(
          FATAL_ERROR
            "MSVC supports only the 'address' sanitizer, but '${sanitizer}' was requested"
        )
      endif()
    endforeach()
    add_compile_options(/fsanitize=address)
    # /RTC1 is injected by CMake's default Debug flags and rejected by ASan.
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(CMAKE_C_FLAGS_DEBUG
        "${CMAKE_C_FLAGS_DEBUG}"
        PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}"
        PARENT_SCOPE)
    message(STATUS "Sanitizers enabled: address (MSVC)")
    return()
  endif()

  if(APPLE AND "memory" IN_LIST IQM_QDMI_SANITIZERS)
    message(FATAL_ERROR "MemorySanitizer is not available on macOS")
  endif()

  list(JOIN IQM_QDMI_SANITIZERS "," sanitizer_list)
  set(sanitizer_flags -fsanitize=${sanitizer_list} -fno-omit-frame-pointer)

  if(IQM_QDMI_SANITIZER_HALT_ON_ERROR)
    list(APPEND sanitizer_flags -fno-sanitize-recover=all)
  endif()

  add_compile_options(${sanitizer_flags})
  # The runtime has to be on the link line as well, or the instrumented objects
  # fail to resolve their interceptors.
  add_link_options(-fsanitize=${sanitizer_list})

  message(STATUS "Sanitizers enabled: ${sanitizer_list}")
endfunction()
