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

execute_process(
  COMMAND ${EXPORT_COMMAND} "${LIBRARY}"
  OUTPUT_VARIABLE exports
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)

if(MSVC)
  # Forwarded exports have no RVA in dumpbin's ordinal, hint, RVA, name table.
  string(
    REGEX
      MATCHALL
      "[\r\n]+[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+([0-9A-Fa-f]+[ \t]+)?[^ \t\r\n]+"
      exports
      "${exports}")
  list(
    TRANSFORM exports
    REPLACE "^[\r\n]+[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+([0-9A-Fa-f]+[ \t]+)?"
            "")
else()
  string(REPLACE "\n" ";" exports "${exports}")
endif()

if(NOT exports)
  message(FATAL_ERROR "No device exports found in ${LIBRARY}")
endif()

foreach(symbol IN LISTS exports)
  string(STRIP "${symbol}" symbol)
  if(APPLE)
    string(REGEX REPLACE "^_" "" symbol "${symbol}")
  endif()
  if(NOT symbol MATCHES "^IQM_QDMI_device_[A-Za-z0-9_]+$")
    message(FATAL_ERROR "Unexpected device export: ${symbol}")
  endif()
endforeach()
