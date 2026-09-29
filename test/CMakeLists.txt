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

add_subdirectory(fomac)
add_subdirectory(integration)

set(QDMI_PREFIX "IQM")
generate_device_defs_executable(${QDMI_PREFIX} TARGET iqm-qdmi-device)

if(MSVC)
  get_filename_component(IQM_QDMI_LINKER_DIR "${CMAKE_LINKER}" DIRECTORY)
  # ClangCL's linker can live outside the MSVC tools directory.
  if(CMAKE_GENERATOR_INSTANCE)
    file(GLOB IQM_QDMI_MSVC_TOOL_DIRS
         "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/MSVC/*/bin/Host*/*")
  endif()
  find_program(
    IQM_QDMI_DUMPBIN dumpbin HINTS "${IQM_QDMI_LINKER_DIR}"
                                   ${IQM_QDMI_MSVC_TOOL_DIRS} REQUIRED)
  set(IQM_QDMI_EXPORT_COMMAND "${IQM_QDMI_DUMPBIN};/nologo;/exports")
elseif(APPLE)
  set(IQM_QDMI_EXPORT_COMMAND "${CMAKE_NM};-gUj")
elseif(UNIX)
  set(IQM_QDMI_EXPORT_COMMAND "${CMAKE_NM};-D;--defined-only;-j")
endif()

if(MSVC OR UNIX)
  add_test(
    NAME DeviceExportsOnlyQdmiAbi
    COMMAND
      ${CMAKE_COMMAND} "-DEXPORT_COMMAND=${IQM_QDMI_EXPORT_COMMAND}"
      "-DLIBRARY=$<TARGET_FILE:iqm-qdmi-device>" "-DMSVC=${MSVC}"
      "-DAPPLE=${APPLE}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/check_device_exports.cmake")
endif()

if(NOT USE_INSTALLED_IQM_QDMI_DEVICE)
  add_subdirectory(unit)
endif()
