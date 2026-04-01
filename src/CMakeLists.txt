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

include(GNUInstallDirs)

# Generate QDMI headers with the specified prefix
generate_prefixed_qdmi_headers(${QDMI_PREFIX})

# Collect source files
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE PRIVATE_HEADER_FILES "${PROJECT_SOURCE_DIR}/include/*.hpp")
file(GLOB_RECURSE PUBLIC_HEADER_FILES "${CMAKE_CURRENT_BINARY_DIR}/include/*.h")

add_library(${QDMI_TARGET_NAME} SHARED)
target_compile_features(${QDMI_TARGET_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${QDMI_TARGET_NAME}
                           PRIVATE QDMI_VERSION="${QDMI_VERSION}")
target_compile_definitions(
  ${QDMI_TARGET_NAME} PRIVATE IQM_QDMI_DEVICE_VERSION="${CMAKE_PROJECT_VERSION}"
                              IQM_QDMI_device_EXPORTS)
set_target_properties(
  ${QDMI_TARGET_NAME}
  PROPERTIES C_VISIBILITY_PRESET hidden
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN 1)
target_sources(${QDMI_TARGET_NAME} PRIVATE ${SRC_FILES})

target_sources(
  ${QDMI_TARGET_NAME}
  PRIVATE FILE_SET
          private_headers
          TYPE
          HEADERS
          BASE_DIRS
          ${PROJECT_SOURCE_DIR}/include
          FILES
          ${PRIVATE_HEADER_FILES}
  PUBLIC FILE_SET
         public_headers
         TYPE
         HEADERS
         BASE_DIRS
         ${CMAKE_CURRENT_BINARY_DIR}/include
         FILES
         ${PUBLIC_HEADER_FILES})

if(TARGET qdmi::qdmi)
  get_target_property(QDMI_INCLUDE_DIRS qdmi::qdmi
                      INTERFACE_INCLUDE_DIRECTORIES)
  if(QDMI_INCLUDE_DIRS)
    target_include_directories(${QDMI_TARGET_NAME} PUBLIC ${QDMI_INCLUDE_DIRS})
  endif()
endif()

target_link_libraries(
  ${QDMI_TARGET_NAME} PRIVATE qdmi::qdmi_project_warnings CURL::libcurl
                              nlohmann_json::nlohmann_json)

# ------------------------------------------------------------------------------
# Coverage
# ------------------------------------------------------------------------------
option(IQM_QDMI_COVERAGE "Enable coverage instrumentation" OFF)

if(IQM_QDMI_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Building with coverage instrumentation")
    target_compile_options(${QDMI_TARGET_NAME} PUBLIC --coverage -O0 -g)
    target_link_options(${QDMI_TARGET_NAME} PUBLIC --coverage)
  else()
    message(WARNING "Coverage instrumentation is only supported with GCC/Clang")
  endif()
endif()

set(IQM_QDMI_CONFIG_INSTALL_DIR
    "${CMAKE_INSTALL_DATADIR}/cmake/${QDMI_TARGET_NAME}"
    CACHE INTERNAL "Config install directory")

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/${QDMI_TARGET_NAME}-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${QDMI_TARGET_NAME}-config.cmake"
  INSTALL_DESTINATION ${IQM_QDMI_CONFIG_INSTALL_DIR}
  NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO)
write_basic_package_version_file(
  "${QDMI_TARGET_NAME}-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${QDMI_TARGET_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${QDMI_TARGET_NAME}-config-version.cmake"
  DESTINATION ${IQM_QDMI_CONFIG_INSTALL_DIR})

# cmake-format: off
install(
  TARGETS ${QDMI_TARGET_NAME}
  EXPORT ${QDMI_TARGET_NAME}-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          COMPONENT ${QDMI_TARGET_NAME}_Runtime
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          COMPONENT ${QDMI_TARGET_NAME}_Runtime
          NAMELINK_COMPONENT ${QDMI_TARGET_NAME}_Development
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          COMPONENT ${QDMI_TARGET_NAME}_Development
  FILE_SET public_headers
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# cmake-format: on

install(
  EXPORT ${QDMI_TARGET_NAME}-targets
  FILE ${QDMI_TARGET_NAME}-targets.cmake
  DESTINATION ${IQM_QDMI_CONFIG_INSTALL_DIR}
  COMPONENT ${QDMI_TARGET_NAME}_Development)
