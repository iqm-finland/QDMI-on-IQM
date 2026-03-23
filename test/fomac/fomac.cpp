/*
 * Copyright (c) 2025 - 2026 IQM QDMI developers
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @file
 * @brief FoMaC Implementation for testing the IQM QDMI Device.
 */

#include "fomac.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iqm_qdmi/device.h>
#include <map>
#include <optional>
#include <qdmi/constants.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

auto FoMaC::throw_if_error(const int status, const std::string &message)
    -> void {
  if (status == QDMI_SUCCESS) {
    return;
  }

  if (status == QDMI_WARN_GENERAL) {
    if (message.empty()) {
      std::cerr << "A general warning.\n";
      return;
    }
    std::cerr << message << '\n';
    return;
  }

  if (!message.empty()) {
    if (status == QDMI_ERROR_INVALIDARGUMENT) {
      throw std::invalid_argument(message);
    }
    throw std::runtime_error(message);
  }

  if (status == QDMI_ERROR_FATAL) {
    throw std::runtime_error("A fatal error.");
  }
  if (status == QDMI_ERROR_OUTOFMEM) {
    throw std::bad_alloc();
  }
  if (status == QDMI_ERROR_NOTIMPLEMENTED) {
    throw std::runtime_error("Not implemented.");
  }
  if (status == QDMI_ERROR_LIBNOTFOUND) {
    throw std::runtime_error("Library not found.");
  }
  if (status == QDMI_ERROR_NOTFOUND) {
    throw std::runtime_error("Element not found.");
  }
  if (status == QDMI_ERROR_OUTOFRANGE) {
    throw std::out_of_range("Out of range.");
  }
  if (status == QDMI_ERROR_INVALIDARGUMENT) {
    throw std::invalid_argument("Invalid argument.");
  }
  if (status == QDMI_ERROR_PERMISSIONDENIED) {
    throw std::runtime_error("Permission denied.");
  }
  if (status == QDMI_ERROR_NOTSUPPORTED) {
    throw std::runtime_error("Operation is not supported.");
  }
}

IQM_QDMI_Device_Session
FoMaC::get_iqm_session(const std::string &base_url,
                       const std::optional<std::string> &token,
                       const std::optional<std::string> &tokens_file,
                       const std::optional<std::string> &qc_id,
                       const std::optional<std::string> &qc_alias) {
  IQM_QDMI_Device_Session session = nullptr;
  auto ret = IQM_QDMI_device_session_alloc(&session);
  throw_if_error(ret, "Failed to allocate IQM QDMI device session.");

  // Set the session parameters
  ret = IQM_QDMI_device_session_set_parameter(
      session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, base_url.size() + 1,
      base_url.c_str());
  throw_if_error(ret, "Failed to set the base URL for the device session.");

  if (token.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_TOKEN, token->size() + 1,
        token->c_str());
    throw_if_error(ret, "Failed to set the token for the device session.");
  }

  if (tokens_file.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE,
        tokens_file->size() + 1, tokens_file->c_str());
    throw_if_error(ret,
                   "Failed to set the tokens file for the device session.");
  }

  if (qc_id.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1, qc_id->size() + 1,
        qc_id->c_str());
    throw_if_error(
        ret, "Failed to set the quantum computer ID for the device session.");
  }

  if (qc_alias.has_value()) {
    ret = IQM_QDMI_device_session_set_parameter(
        session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2, qc_alias->size() + 1,
        qc_alias->c_str());
    throw_if_error(
        ret,
        "Failed to set the quantum computer alias for the device session.");
  }

  ret = IQM_QDMI_device_session_init(session);
  throw_if_error(ret, "Failed to initialize the device session.");

  return session;
}

auto FoMaC::get_name() const -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_NAME, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the device name size");
  std::string name(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_NAME, size, name.data(), nullptr);
  throw_if_error(ret2, "Failed to query the device name");
  return name;
}

auto FoMaC::get_version() const -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_VERSION, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the device version size");
  std::string version(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_VERSION, size, version.data(), nullptr);
  throw_if_error(ret2, "Failed to query the device version");
  return version;
}

auto FoMaC::get_library_version() const -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_LIBRARYVERSION, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the library version size");
  std::string version(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_LIBRARYVERSION, size, version.data(),
      nullptr);
  throw_if_error(ret2, "Failed to query the library version");
  return version;
}

auto FoMaC::get_qubits_num() const -> size_t {
  size_t num_qubits = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_QUBITSNUM, sizeof(size_t), &num_qubits,
      nullptr);
  throw_if_error(ret, "Failed to query the number of qubits.");
  return num_qubits;
}

auto FoMaC::get_operation_map() const
    -> std::map<std::string, IQM_QDMI_Operation> {
  size_t ops_size = 0;
  int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_OPERATIONS, 0, nullptr, &ops_size);
  throw_if_error(ret, "Failed to get the operations list size.");
  std::vector<IQM_QDMI_Operation> ops(ops_size / sizeof(IQM_QDMI_Operation));
  ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_OPERATIONS, ops_size,
      static_cast<void *>(ops.data()), nullptr);
  throw_if_error(ret, "Failed to retrieve operations.");
  std::map<std::string, IQM_QDMI_Operation> ops_map;
  for (const auto &op : ops) {
    size_t name_length = 0;
    ret = IQM_QDMI_device_session_query_operation_property(
        session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_NAME, 0,
        nullptr, &name_length);
    throw_if_error(ret, "Failed to retrieve operation name length.");
    std::string name(name_length - 1, '\0');
    ret = IQM_QDMI_device_session_query_operation_property(
        session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_NAME,
        name_length, name.data(), nullptr);
    throw_if_error(ret, "Failed to retrieve operation name.");
    ops_map.emplace(name, op);
  }
  return ops_map;
}

auto FoMaC::get_coupling_map() const
    -> std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>> {
  size_t size = 0;
  int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_COUPLINGMAP, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the coupling map size.");

  const auto coupling_map_size = size / sizeof(IQM_QDMI_Site);
  if (coupling_map_size % 2 != 0) {
    throw std::runtime_error("The coupling map needs to have an even number of "
                             "elements.");
  }

  // `std::vector` guarantees that the elements are contiguous in memory.
  std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>> coupling_pairs(
      coupling_map_size / 2);
  ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_COUPLINGMAP, size,
      static_cast<void *>(coupling_pairs.data()), nullptr);
  throw_if_error(ret, "Failed to query the coupling map.");
  return coupling_pairs;
}

auto FoMaC::get_sites() const -> std::vector<IQM_QDMI_Site> {
  size_t sites_size = 0;
  int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_SITES, 0, nullptr, &sites_size);
  throw_if_error(ret, "Failed to get the sites list size.");
  std::vector<IQM_QDMI_Site> sites(sites_size / sizeof(IQM_QDMI_Site));
  ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_SITES, sites_size,
      static_cast<void *>(sites.data()), nullptr);
  throw_if_error(ret, "Failed to get the sites.");
  return sites;
}

auto FoMaC::get_duration_unit() const -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_DURATIONUNIT, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the duration unit size.");
  std::string unit(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_DURATIONUNIT, size, unit.data(), nullptr);
  throw_if_error(ret2, "Failed to query the duration unit.");
  return unit;
}

auto FoMaC::get_duration_scale_factor() const -> double {
  double scale_factor = 0;
  const int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_DURATIONSCALEFACTOR, sizeof(double),
      &scale_factor, nullptr);
  throw_if_error(ret, "Failed to query the duration scale factor.");
  return scale_factor;
}

auto FoMaC::get_site_index(IQM_QDMI_Site site) const -> uint64_t {
  uint64_t site_id = 0;
  const int ret = IQM_QDMI_device_session_query_site_property(
      session_, site, QDMI_SITE_PROPERTY_INDEX, sizeof(uint64_t), &site_id,
      nullptr);
  throw_if_error(ret, "Failed to query the site ID");
  return site_id;
}

auto FoMaC::get_site_t1(IQM_QDMI_Site site) const -> uint64_t {
  uint64_t t1 = 0;
  const int ret = IQM_QDMI_device_session_query_site_property(
      session_, site, QDMI_SITE_PROPERTY_T1, sizeof(uint64_t), &t1, nullptr);
  throw_if_error(ret, "Failed to query the T1 time for " + get_site_name(site));
  return t1;
}

auto FoMaC::get_site_t2(IQM_QDMI_Site site) const -> uint64_t {
  uint64_t t2 = 0;
  const int ret = IQM_QDMI_device_session_query_site_property(
      session_, site, QDMI_SITE_PROPERTY_T2, sizeof(uint64_t), &t2, nullptr);
  throw_if_error(ret, "Failed to query the T2 time for " + get_site_name(site));
  return t2;
}

auto FoMaC::get_site_name(IQM_QDMI_Site site) const -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_session_query_site_property(
      session_, site, QDMI_SITE_PROPERTY_NAME, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the site name length");
  std::string custom(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_site_property(
      session_, site, QDMI_SITE_PROPERTY_NAME, size, custom.data(), nullptr);
  throw_if_error(ret2, "Failed to query the site name");
  return custom;
}

auto FoMaC::get_operation_name(const IQM_QDMI_Operation &op) const
    -> std::string {
  size_t name_length = 0;
  const int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_NAME, 0,
      nullptr, &name_length);
  throw_if_error(ret, "Failed to query the operation name length");
  std::string name(name_length - 1, '\0');
  const int ret2 = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_NAME,
      name_length, name.data(), nullptr);
  throw_if_error(ret2, "Failed to query the operation name");
  return name;
}

auto FoMaC::get_operation_operands_num(const IQM_QDMI_Operation &op) const
    -> size_t {
  size_t operands_num = 0;
  const int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_QUBITSNUM,
      sizeof(size_t), &operands_num, nullptr);
  throw_if_error(ret, "Failed to query the operand number for " +
                          get_operation_name(op));
  return operands_num;
}

auto FoMaC::get_operation_parameters_num(const IQM_QDMI_Operation &op) const
    -> size_t {
  size_t parameters_num = 0;
  const int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr,
      QDMI_OPERATION_PROPERTY_PARAMETERSNUM, sizeof(size_t), &parameters_num,
      nullptr);
  throw_if_error(ret, "Failed to query the parameter number for " +
                          get_operation_name(op));
  return parameters_num;
}

auto FoMaC::get_operation_fidelity(const IQM_QDMI_Operation &op,
                                   const std::vector<IQM_QDMI_Site> &sites,
                                   const std::vector<double> &params) const
    -> double {
  double fidelity = 0;
  const int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, sites.size(), sites.data(), params.size(), params.data(),
      QDMI_OPERATION_PROPERTY_FIDELITY, sizeof(double), &fidelity, nullptr);
  throw_if_error(ret, "Failed to query the operation fidelity for " +
                          get_operation_name(op));
  return fidelity;
}

auto FoMaC::get_operation_duration(const IQM_QDMI_Operation &op,
                                   const std::vector<IQM_QDMI_Site> &sites,
                                   const std::vector<double> &params) const
    -> double {
  double duration = 0;
  const int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, sites.size(), sites.data(), params.size(), params.data(),
      QDMI_OPERATION_PROPERTY_DURATION, sizeof(double), &duration, nullptr);
  throw_if_error(ret, "Failed to query the operation duration for " +
                          get_operation_name(op));
  return duration;
}

auto FoMaC::get_operation_sites(const IQM_QDMI_Operation &op) const
    -> std::vector<IQM_QDMI_Site> {
  size_t size = 0;
  int ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_SITES, 0,
      nullptr, &size);
  throw_if_error(ret, "Failed to query the operation sites size for " +
                          get_operation_name(op));
  std::vector<IQM_QDMI_Site> sites(size / sizeof(IQM_QDMI_Site));
  ret = IQM_QDMI_device_session_query_operation_property(
      session_, op, 0, nullptr, 0, nullptr, QDMI_OPERATION_PROPERTY_SITES, size,
      static_cast<void *>(sites.data()), nullptr);
  throw_if_error(ret, "Failed to query the operation sites for " +
                          get_operation_name(op));
  return sites;
}

auto FoMaC::get_supported_program_formats() const
    -> std::vector<QDMI_Program_Format> {
  size_t size = 0;
  int ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_SUPPORTEDPROGRAMFORMATS, 0, nullptr,
      &size);
  throw_if_error(ret, "Failed to query the supported program formats size.");
  std::vector<QDMI_Program_Format> formats(size / sizeof(QDMI_Program_Format));
  ret = IQM_QDMI_device_session_query_device_property(
      session_, QDMI_DEVICE_PROPERTY_SUPPORTEDPROGRAMFORMATS, size,
      static_cast<void *>(formats.data()), nullptr);
  throw_if_error(ret, "Failed to query the supported program formats.");
  return formats;
}

auto FoMaC::submit_job(
    const std::string &program, const QDMI_Program_Format format,
    const size_t num_shots, const std::string &heralding_mode,
    const std::string &move_validation_mode,
    const std::string &move_gate_frame_tracking_mode,
    const std::string &dd_mode,
    const std::optional<std::map<std::string, std::string>> &qubit_mapping,
    const std::optional<double> &max_circuit_duration_over_t2,
    const std::optional<size_t> &num_active_reset_cycles,
    const std::optional<std::string> &dd_strategy) const
    -> IQM_QDMI_Device_Job {
  IQM_QDMI_Device_Job job = nullptr;
  int ret = IQM_QDMI_device_session_create_device_job(session_, &job);
  throw_if_error(ret, "Failed to create a job");
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(QDMI_Program_Format),
      &format);
  throw_if_error(ret, "Failed to set the program format");
  ret =
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                                        program.size() + 1, program.c_str());
  throw_if_error(ret, "Failed to set the program");
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(size_t), &num_shots);
  throw_if_error(ret, "Failed to set the number of shots");
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM1, heralding_mode.size() + 1,
      heralding_mode.c_str());
  throw_if_error(ret, "Failed to set the heralding mode");
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM2, move_validation_mode.size() + 1,
      move_validation_mode.c_str());
  throw_if_error(ret, "Failed to set the move validation mode");
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM3,
      move_gate_frame_tracking_mode.size() + 1,
      move_gate_frame_tracking_mode.c_str());
  throw_if_error(ret, "Failed to set the move gate frame tracking mode");
  ret =
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM4,
                                        dd_mode.size() + 1, dd_mode.c_str());
  throw_if_error(ret, "Failed to set the dynamical decoupling mode");
  if (qubit_mapping.has_value()) {
    std::string mapping_str;
    for (const auto &pair : *qubit_mapping) {
      mapping_str += pair.first + ":" + pair.second + ",";
    }
    if (!mapping_str.empty()) {
      mapping_str.pop_back(); // Remove the trailing comma
    }
    ret = IQM_QDMI_device_job_set_parameter(
        job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM5, mapping_str.size() + 1,
        mapping_str.c_str());
    throw_if_error(ret, "Failed to set the qubit mapping");
  }
  if (max_circuit_duration_over_t2.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 1),
        sizeof(double), &max_circuit_duration_over_t2.value());
    throw_if_error(ret, "Failed to set the maximum circuit duration over T2");
  }
  if (num_active_reset_cycles.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2),
        sizeof(size_t), &num_active_reset_cycles.value());
    throw_if_error(ret, "Failed to set the number of active reset cycles");
  }
  if (dd_strategy.has_value()) {
    ret = IQM_QDMI_device_job_set_parameter(
        job,
        // NOLINTNEXTLINE
        static_cast<QDMI_Device_Job_Parameter>(
            QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 3),
        dd_strategy->size() + 1, dd_strategy->c_str());
    throw_if_error(ret, "Failed to set the dynamical decoupling strategy");
  }
  ret = IQM_QDMI_device_job_submit(job);
  throw_if_error(ret, "Failed to submit the job");
  return job;
}

auto FoMaC::get_status(IQM_QDMI_Device_Job job) -> QDMI_Job_Status {
  QDMI_Job_Status status{};
  const int ret = IQM_QDMI_device_job_check(job, &status);
  throw_if_error(ret, "Failed to check the job status");
  return status;
}

auto FoMaC::wait(IQM_QDMI_Device_Job job, const size_t timeout) -> void {
  const int ret = IQM_QDMI_device_job_wait(job, timeout);
  throw_if_error(ret, "Failed to wait for the job");
}

auto FoMaC::cancel(IQM_QDMI_Device_Job job) -> void {
  const int ret = IQM_QDMI_device_job_cancel(job);
  throw_if_error(ret, "Failed to cancel the job");
}

auto FoMaC::get_job_id(IQM_QDMI_Device_Job job) -> std::string {
  size_t job_id_size = 0;
  const int ret = IQM_QDMI_device_job_query_property(
      job, QDMI_DEVICE_JOB_PROPERTY_ID, 0, nullptr, &job_id_size);
  throw_if_error(ret, "Failed to query the job ID size");
  std::string job_id(job_id_size - 1, '\0');
  const int ret2 = IQM_QDMI_device_job_query_property(
      job, QDMI_DEVICE_JOB_PROPERTY_ID, job_id_size, job_id.data(), nullptr);
  throw_if_error(ret2, "Failed to query the job ID");
  return job_id;
}

auto FoMaC::get_job_shots_num(IQM_QDMI_Device_Job job) -> size_t {
  size_t num_shots = 0;
  const int ret =
      IQM_QDMI_device_job_query_property(job, QDMI_DEVICE_JOB_PROPERTY_SHOTSNUM,
                                         sizeof(size_t), &num_shots, nullptr);
  throw_if_error(ret, "Failed to query the number of shots");
  return num_shots;
}

auto FoMaC::get_histogram(IQM_QDMI_Device_Job job)
    -> std::map<std::string, size_t> {
  size_t size = 0;
  const int ret = IQM_QDMI_device_job_get_results(
      job, QDMI_JOB_RESULT_HIST_KEYS, 0, nullptr, &size);
  throw_if_error(ret, "Failed to query the histogram keys size");
  std::vector<char> key_buffer(size);
  const int ret2 = IQM_QDMI_device_job_get_results(
      job, QDMI_JOB_RESULT_HIST_KEYS, size, key_buffer.data(), nullptr);
  throw_if_error(ret2, "Failed to query the histogram keys");
  const std::string key_list(key_buffer.data());
  std::vector<std::string> key_vec;
  std::string token;
  std::stringstream ss(key_list);
  while (std::getline(ss, token, ',')) {
    key_vec.emplace_back(token);
  }

  size_t val_size = 0;
  const int ret3 = IQM_QDMI_device_job_get_results(
      job, QDMI_JOB_RESULT_HIST_VALUES, 0, nullptr, &val_size);
  throw_if_error(ret3, "Failed to query the histogram values size");
  std::vector<size_t> val_vec(key_vec.size());
  const int ret4 = IQM_QDMI_device_job_get_results(
      job, QDMI_JOB_RESULT_HIST_VALUES, val_size, val_vec.data(), nullptr);
  throw_if_error(ret4, "Failed to query the histogram values");
  std::map<std::string, size_t> results;
  for (size_t i = 0; i < key_vec.size(); ++i) {
    results[key_vec[i]] = val_vec[i];
  }
  return results;
}

auto FoMaC::get_calibration_set_id(IQM_QDMI_Device_Job job) -> std::string {
  size_t size = 0;
  const int ret = IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM1,
                                                  0, nullptr, &size);
  throw_if_error(ret, "Failed to query the calibration set ID size");
  std::string calibration_set_id(size - 1, '\0');
  const int ret2 = IQM_QDMI_device_job_get_results(
      job, QDMI_JOB_RESULT_CUSTOM1, size, calibration_set_id.data(), nullptr);
  throw_if_error(ret2, "Failed to query the calibration set ID");
  return calibration_set_id;
}
