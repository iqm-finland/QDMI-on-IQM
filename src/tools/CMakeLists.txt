# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Standalone CLI tool for direct circuit submission
set(IQM_QDMI_RUNNER_NAME iqm-qdmi-runner)

add_executable(${IQM_QDMI_RUNNER_NAME} iqm_qdmi_runner.cpp)
target_compile_features(${IQM_QDMI_RUNNER_NAME} PRIVATE cxx_std_20)
target_link_libraries(${IQM_QDMI_RUNNER_NAME} PRIVATE ${QDMI_TARGET_NAME})

# Expose runner target name to the parent CMakeLists for install/export wiring.
set(IQM_QDMI_RUNNER_NAME
    ${IQM_QDMI_RUNNER_NAME}
    PARENT_SCOPE)
