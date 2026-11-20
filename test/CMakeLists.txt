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

add_executable(iteration_exports iteration/iteration_exports.c)
target_link_libraries(iteration_exports PRIVATE ${QDMI_TARGET_NAME})
add_test(NAME IterationExports COMMAND iteration_exports)
if(WIN32)
  add_custom_command(
    TARGET iteration_exports
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${QDMI_TARGET_NAME}>
      $<TARGET_FILE_DIR:iteration_exports>)
endif()
add_subdirectory(qdmi_client)
add_subdirectory(integration)

set(QDMI_PREFIX "IQM")
generate_device_defs_executable(${QDMI_PREFIX} TARGET iqm-qdmi-device)

if(NOT USE_INSTALLED_IQM_QDMI_DEVICE)
  add_subdirectory(unit)
endif()

add_subdirectory(iteration)
