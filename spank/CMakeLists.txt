# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

set(BUILD_IQM_SPANK_TARGET iqm-spank-plugin)
add_library(${BUILD_IQM_SPANK_TARGET} MODULE iqm_spank_plugin.cpp)
target_compile_features(${BUILD_IQM_SPANK_TARGET} PRIVATE cxx_std_20)
target_include_directories(${BUILD_IQM_SPANK_TARGET}
                           PRIVATE ${SLURM_SPANK_INCLUDE_DIR})
set_target_properties(${BUILD_IQM_SPANK_TARGET} PROPERTIES PREFIX "")

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
      "Install directory for the IQM SPANK plugin (override for distro-specific Slurm plugin paths)"
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

install(TARGETS ${BUILD_IQM_SPANK_TARGET}
        LIBRARY DESTINATION ${IQM_QDMI_SPANK_INSTALL_DIR}
                COMPONENT iqm_spank_plugin)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/iqm-qdmi.conf
  DESTINATION ${IQM_QDMI_SLURM_CONF_DIR}/plugstack.conf.d
  COMPONENT iqm_spank_plugin)
