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
set(package iqm-qdmi-device)
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(work "${source}/build/install-check")
string(
  RANDOM
  LENGTH 8
  ALPHABET 0123456789abcdef suffix)
set(prefix "${work}/prefix-${suffix}")
set(relocated "${work}/relocated-${suffix}")

function(run)
  execute_process(COMMAND ${ARGV} COMMAND_ERROR_IS_FATAL ANY)
endfunction()

run("${CMAKE_COMMAND}"
    -S
    "${source}"
    -B
    "${work}/producer"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_IQM_QDMI_TESTS=OFF)
run("${CMAKE_COMMAND}" --build "${work}/producer" --config Release)
foreach(component Runtime Development)
  run("${CMAKE_COMMAND}"
      --install
      "${work}/producer"
      --config
      Release
      --prefix
      "${prefix}"
      --component
      "${package}_${component}")
endforeach()
file(RENAME "${prefix}" "${relocated}")

run("${CMAKE_COMMAND}"
    -S
    "${source}/test/install_consumer"
    -B
    "${work}/consumer-${suffix}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=${relocated}")
run("${CMAKE_COMMAND}" --build "${work}/consumer-${suffix}" --config Release)
run("${CMAKE_CTEST_COMMAND}"
    --test-dir
    "${work}/consumer-${suffix}"
    -C
    Release
    --output-on-failure
    --no-tests=error)
