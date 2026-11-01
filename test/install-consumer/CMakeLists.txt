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

cmake_minimum_required(VERSION 3.24)
project(iqm-installed-consumer LANGUAGES C)
if(NOT TARGET iqm-qdmi-device)
  find_package(iqm-qdmi-device 1.5 REQUIRED CONFIG)
endif()
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE iqm-qdmi-device)
enable_testing()
add_test(NAME installed-consumer COMMAND consumer)
if(WIN32)
  add_custom_command(
    TARGET consumer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:iqm-qdmi-device> $<TARGET_FILE_DIR:consumer>)
endif()
