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

add_executable(iteration_management_smoke iteration_management_smoke.cpp)
target_compile_features(iteration_management_smoke PRIVATE cxx_std_20)
target_link_libraries(iteration_management_smoke
                      PRIVATE ${QDMI_TARGET_NAME} qdmi::qdmi_project_warnings)
# Deliberately invoked directly: live mutations must never enter default CTest.
if(WIN32)
  add_custom_command(
    TARGET iteration_management_smoke
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${QDMI_TARGET_NAME}>
      $<TARGET_FILE_DIR:iteration_management_smoke>)
endif()
