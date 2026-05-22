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

set(BUILD_IQM_QDMI_SPANK_TARGET iqm-qdmi-spank-plugin)
add_library(${BUILD_IQM_QDMI_SPANK_TARGET} MODULE iqm_qdmi_spank_plugin.cpp)
target_compile_features(${BUILD_IQM_QDMI_SPANK_TARGET} PRIVATE cxx_std_20)
target_include_directories(${BUILD_IQM_QDMI_SPANK_TARGET}
                           PRIVATE ${SLURM_SPANK_INCLUDE_DIR})
set_target_properties(${BUILD_IQM_QDMI_SPANK_TARGET} PROPERTIES PREFIX "")

# Detect the Slurm plugin directory for the SPANK plugin.
set(IQM_QDMI_SPANK_INSTALL_DIR_DEFAULT "/usr/lib/slurm")
find_program(IQM_QDMI_SCONTROL_EXECUTABLE scontrol)
if(IQM_QDMI_SCONTROL_EXECUTABLE)
  execute_process(
    COMMAND ${IQM_QDMI_SCONTROL_EXECUTABLE} show config
    OUTPUT_VARIABLE IQM_QDMI_SLURM_CONFIG
    RESULT_VARIABLE IQM_QDMI_SCONTROL_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

  if(IQM_QDMI_SCONTROL_RESULT EQUAL 0)
    string(REGEX MATCH "PluginDir[ \t]*=[ \t]*([^ \t\r\n]+)"
                 IQM_QDMI_PLUGIN_DIR_MATCH "${IQM_QDMI_SLURM_CONFIG}")
    if(CMAKE_MATCH_1)
      set(IQM_QDMI_SPANK_INSTALL_DIR_DEFAULT "${CMAKE_MATCH_1}")
    endif()
  endif()
endif()

set(IQM_QDMI_SPANK_INSTALL_DIR
    "${IQM_QDMI_SPANK_INSTALL_DIR_DEFAULT}"
    CACHE
      PATH
      "Install directory for the IQM QDMI device SPANK plugin (override for distro-specific Slurm plugin paths)"
)

mark_as_advanced(IQM_QDMI_SCONTROL_EXECUTABLE)

# Detect the Slurm configuration directory for the plugstack.conf.d drop-in.
set(IQM_QDMI_SLURM_CONF_DIR_DEFAULT "/etc/slurm")
if(IQM_QDMI_SCONTROL_EXECUTABLE AND IQM_QDMI_SCONTROL_RESULT EQUAL 0)
  string(REGEX MATCH "PlugStackConfig[ \t]*=[ \t]*([^ \t\r\n]+)"
               IQM_QDMI_PLUGSTACK_MATCH "${IQM_QDMI_SLURM_CONFIG}")
  if(CMAKE_MATCH_1)
    get_filename_component(IQM_QDMI_SLURM_CONF_DIR_DEFAULT "${CMAKE_MATCH_1}"
                           DIRECTORY)
  endif()
endif()

set(IQM_QDMI_SLURM_CONF_DIR
    "${IQM_QDMI_SLURM_CONF_DIR_DEFAULT}"
    CACHE PATH "Slurm configuration directory (for plugstack.conf.d drop-in)")

# Generate the plugstack drop-in configuration with the correct plugin path.
configure_file(iqm-qdmi.conf.in iqm-qdmi.conf @ONLY)

install(TARGETS ${BUILD_IQM_QDMI_SPANK_TARGET}
        LIBRARY DESTINATION ${IQM_QDMI_SPANK_INSTALL_DIR}
                COMPONENT iqm_spank_plugin_Runtime)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/iqm-qdmi.conf
  DESTINATION ${IQM_QDMI_SLURM_CONF_DIR}/plugstack.conf.d
  COMPONENT iqm_spank_plugin_Runtime)
