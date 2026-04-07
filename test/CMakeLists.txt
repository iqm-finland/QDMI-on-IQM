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

add_subdirectory(fomac)
if(NOT USE_INSTALLED_IQM_QDMI_DEVICE)
  add_subdirectory(mock)
endif()

set(QDMI_PREFIX "IQM")
generate_device_defs_executable(${QDMI_PREFIX} TARGET iqm-qdmi-device)

if(NOT USE_INSTALLED_IQM_QDMI_DEVICE)
  add_subdirectory(unit)
endif()
add_subdirectory(integration)
