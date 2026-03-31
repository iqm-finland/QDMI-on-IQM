/*
 * Copyright (c) 2025 - 2026 IQM Finland Oy
 * All rights reserved.
 *
 * Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/** @file
 * @brief FoMaC Implementation for testing the IQM QDMI Device.
 */

#pragma once

#include "iqm_qdmi/device.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class FoMaC {
  IQM_QDMI_Device_Session session_;

  static auto throw_if_error(int status, const std::string &message) -> void;

public:
  static IQM_QDMI_Device_Session
  get_iqm_session(const std::string &base_url,
                  const std::optional<std::string> &token = std::nullopt,
                  const std::optional<std::string> &tokens_file = std::nullopt,
                  const std::optional<std::string> &qc_id = std::nullopt,
                  const std::optional<std::string> &qc_alias = std::nullopt);

  FoMaC() = default;
  explicit FoMaC(IQM_QDMI_Device_Session session) : session_(session) {
    assert(session_ != nullptr);
  }

  [[nodiscard]] auto get_name() const -> std::string;

  [[nodiscard]] auto get_version() const -> std::string;

  [[nodiscard]] auto get_library_version() const -> std::string;

  [[nodiscard]] auto get_qubits_num() const -> size_t;

  [[nodiscard]] auto get_operation_map() const
      -> std::map<std::string, IQM_QDMI_Operation>;

  [[nodiscard]] auto get_sites() const -> std::vector<IQM_QDMI_Site>;

  [[nodiscard]] auto get_coupling_map() const
      -> std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>>;

  [[nodiscard]] auto get_duration_unit() const -> std::string;

  [[nodiscard]] auto get_duration_scale_factor() const -> double;

  [[nodiscard]] auto get_site_index(IQM_QDMI_Site site) const -> uint64_t;

  [[nodiscard]] auto get_site_t1(IQM_QDMI_Site site) const -> uint64_t;

  [[nodiscard]] auto get_site_t2(IQM_QDMI_Site site) const -> uint64_t;

  [[nodiscard]] auto get_site_name(IQM_QDMI_Site site) const -> std::string;

  [[nodiscard]] auto get_operation_name(const IQM_QDMI_Operation &op) const
      -> std::string;

  [[nodiscard]] auto
  get_operation_operands_num(const IQM_QDMI_Operation &op) const -> size_t;

  [[nodiscard]] auto
  get_operation_parameters_num(const IQM_QDMI_Operation &op) const -> size_t;

  [[nodiscard]] auto
  get_operation_fidelity(const IQM_QDMI_Operation &op,
                         const std::vector<IQM_QDMI_Site> &sites = {},
                         const std::vector<double> &params = {}) const
      -> double;

  [[nodiscard]] auto
  get_operation_duration(const IQM_QDMI_Operation &op,
                         const std::vector<IQM_QDMI_Site> &sites = {},
                         const std::vector<double> &params = {}) const
      -> double;

  [[nodiscard]] auto get_operation_sites(const IQM_QDMI_Operation &op) const
      -> std::vector<IQM_QDMI_Site>;

  [[nodiscard]] auto get_supported_program_formats() const
      -> std::vector<QDMI_Program_Format>;

  [[nodiscard]] auto submit_job(
      const std::string &program, QDMI_Program_Format format,
      size_t num_shots = 0, const std::string &heralding_mode = "none",
      const std::string &move_validation_mode = "strict",
      const std::string &move_gate_frame_tracking_mode = "full",
      const std::string &dd_mode = "disabled",
      const std::optional<std::map<std::string, std::string>> &qubit_mapping =
          std::nullopt,
      const std::optional<double> &max_circuit_duration_over_t2 = std::nullopt,
      const std::optional<size_t> &num_active_reset_cycles = std::nullopt,
      const std::optional<std::string> &dd_strategy = std::nullopt) const
      -> IQM_QDMI_Device_Job;

  static auto wait(IQM_QDMI_Device_Job job, size_t timeout = 0) -> void;

  static auto cancel(IQM_QDMI_Device_Job job) -> void;

  static auto get_job_id(IQM_QDMI_Device_Job job) -> std::string;

  static auto get_job_shots_num(IQM_QDMI_Device_Job job) -> size_t;

  [[nodiscard]] static auto get_status(IQM_QDMI_Device_Job job)
      -> QDMI_Job_Status;

  [[nodiscard]] static auto get_histogram(IQM_QDMI_Device_Job job)
      -> std::map<std::string, size_t>;

  [[nodiscard]] static auto get_calibration_set_id(IQM_QDMI_Device_Job job)
      -> std::string;
};
