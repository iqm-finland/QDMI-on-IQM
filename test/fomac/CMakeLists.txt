# Copyright (c) 2025 - 2026 IQM QDMI developers
# All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(qdmi_iqm_fomac fomac.cpp fomac.hpp)
target_link_libraries(
  qdmi_iqm_fomac PRIVATE qdmi::qdmi qdmi::qdmi_project_warnings
                         ${QDMI_TARGET_NAME})
target_include_directories(qdmi_iqm_fomac PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(qdmi_iqm_fomac PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(qdmi_iqm_fomac PUBLIC cxx_std_20)
add_library(qdmi::iqm_fomac ALIAS qdmi_iqm_fomac)
