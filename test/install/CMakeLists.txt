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

add_executable(install_public_tests test_iqm_device_public_install.cpp)
target_compile_features(install_public_tests PRIVATE cxx_std_20)
target_link_libraries(
  install_public_tests PRIVATE gtest_main gtest qdmi::qdmi_project_warnings
                               ${QDMI_TARGET_NAME})
gtest_discover_tests(install_public_tests PROPERTIES LABELS "install-public")
