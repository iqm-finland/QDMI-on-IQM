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

find_package(Doxygen 1.15 REQUIRED)

string(TOLOWER "${QDMI_PREFIX}" _qdmi_prefix)
set(_device_headers "${PROJECT_BINARY_DIR}/src/include/${_qdmi_prefix}_qdmi")
if(QDMI_INCLUDE_BUILD_DIR)
  set(_qdmi_headers "${QDMI_INCLUDE_BUILD_DIR}/qdmi")
else()
  set(_qdmi_headers "${qdmi_INCLUDE_DIR}/qdmi")
endif()

set(DOXYGEN_PROJECT_NAME "QDMI on IQM")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/_build/doxygen")
set(DOXYGEN_HTML_OUTPUT cpp)
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_TREEVIEW YES)
set(DOXYGEN_GENERATE_XML NO)
set(DOXYGEN_EXTRACT_ALL YES)
set(DOXYGEN_EXCLUDE "${_device_headers}/constants.h")
set(DOXYGEN_FILE_PATTERNS *.h *.hpp)
set(DOXYGEN_FULL_PATH_NAMES NO)
set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
set(DOXYGEN_MACRO_EXPANSION YES)
set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
set(DOXYGEN_PREDEFINED "${QDMI_PREFIX}_QDMI_EXPORT=" "QDMI_EXPORT="
                       "QDMI_DRIVER_EXPORT=")
set(DOXYGEN_QUIET YES)
set(DOXYGEN_WARN_AS_ERROR YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
set(DOXYGEN_WARN_NO_PARAMDOC YES)

doxygen_add_docs(
  ${QDMI_TARGET_NAME}-docs "${_device_headers}" "${_qdmi_headers}"
  "${PROJECT_SOURCE_DIR}/include/iqm_qdmi"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
add_dependencies(${QDMI_TARGET_NAME} ${QDMI_TARGET_NAME}-docs)
