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
 * @brief QDMI Device Implementation for the IQM Server API.
 * @details For the documentation of the functions, see the official
 * documentation at https://munich-quantum-software-stack.github.io/QDMI.
 */

#include "curl_http_client.hpp"
#include "http_client.hpp"
#include "iqm_api_config.hpp"
#include "iqm_auth.hpp"
#include "iqm_qdmi/device.h"
#include "logging.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

struct IQM_QDMI_Operation_impl_d;
struct IQM_QDMI_Site_impl_d;

enum class IQM_QDMI_DEVICE_SESSION_STATUS : uint8_t { ALLOCATED, INITIALIZED };

/**
 * @brief Implementation of the IQM_QDMI_Device_Session structure.
 */
struct IQM_QDMI_Device_Session_impl_d {
  /// Base URL of the device.
  std::string base_url_;

  /// Authentication parameters
  std::optional<std::string> token_ = std::nullopt;
  std::optional<std::string> tokens_file_ = std::nullopt;

  /// HTTP client
  std::unique_ptr<iqm::IHttpClient> http_client_;

  /// Authentication manager
  std::unique_ptr<iqm::TokenManager> token_manager_;

  /// API configuration
  std::unique_ptr<iqm::APIConfig> api_config_;

  /// Session status
  IQM_QDMI_DEVICE_SESSION_STATUS session_status_ =
      IQM_QDMI_DEVICE_SESSION_STATUS::ALLOCATED;

  /// Device status
  QDMI_Device_Status device_status_ = QDMI_DEVICE_STATUS_OFFLINE;

  /// Quantum computer ID
  std::optional<std::string> quantum_computer_id_ = std::nullopt;

  /// Quantum computer alias
  std::optional<std::string> quantum_computer_alias_ = std::nullopt;

  /// Calibration set id
  std::string calibration_set_id_;

  /// Supports calibration jobs
  bool supports_calibration_jobs_ = false;

  /// Sites of the device
  std::vector<std::unique_ptr<IQM_QDMI_Site_impl_d>> sites_;
  std::vector<IQM_QDMI_Site_impl_d *> sites_ptr_;
  std::unordered_map<std::string, IQM_QDMI_Site_impl_d *> sites_map_;

  /// Site connectivity
  std::vector<std::pair<IQM_QDMI_Site_impl_d *, IQM_QDMI_Site_impl_d *>>
      connectivity_;

  /// Operations of the device
  std::vector<std::unique_ptr<IQM_QDMI_Operation_impl_d>> operations_;
  std::vector<IQM_QDMI_Operation_impl_d *> operations_ptr_;
  std::unordered_map<std::string, IQM_QDMI_Operation_impl_d *> operations_map_;
  std::unordered_map<IQM_QDMI_Operation_impl_d *,
                     std::vector<std::vector<IQM_QDMI_Site_impl_d *>>>
      operations_sites_map_;
  std::unordered_map<IQM_QDMI_Operation_impl_d *,
                     std::unordered_map<IQM_QDMI_Site_impl_d *, double>>
      operations_single_qubit_fidelity_map_;

  struct Pair_hash {
    template <class T1, class T2>
    size_t operator()(const std::pair<T1, T2> &p) const {
      auto hash1 = std::hash<T1>{}(p.first);
      auto hash2 = std::hash<T2>{}(p.second);
      return hash1 ^ hash2;
    }
  };

  std::unordered_map<IQM_QDMI_Operation_impl_d *,
                     std::unordered_map<std::pair<IQM_QDMI_Site_impl_d *,
                                                  IQM_QDMI_Site_impl_d *>,
                                        double, Pair_hash>>
      operations_two_qubit_fidelity_map_;
};

/**
 * @brief Implementation of the IQM_QDMI_Device_Job structure.
 */
struct IQM_QDMI_Device_Job_impl_d {
  /// The session this job belongs to.
  IQM_QDMI_Device_Session session_ = nullptr;
  /// The job ID as returned by the API.
  std::string job_id_;
  /// The program format used for this job.
  QDMI_Program_Format program_format_ = QDMI_PROGRAM_FORMAT_IQMJSON;
  /// The program to be executed.
  void *program_ = nullptr;
  /// The number of shots to execute for a quantum circuit job.
  size_t num_shots_ = 0;
  /// @brief Heralding mode for the job.
  /// @details Valid values include "none" and "zeros".
  ///          Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM1 parameter.
  std::string heralding_mode_ = "none";
  /// @brief Move validation mode for the job.
  /// @details Valid values include "strict", "allow_prx", and "none"
  ///          Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM2 parameter.
  std::string move_validation_mode_ = "strict";
  /// @brief Move gate frame tracking mode for the job.
  /// @details Valid values include "full", "no_detuning_correction", and
  ///          "none".
  ///          Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM3 parameter.
  std::string move_gate_frame_tracking_mode_ = "full";
  /// @brief Dynamical decoupling mode for the job.
  /// @details Valid values include "disabled" and "enabled".
  ///          Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM4 parameter.
  std::string dd_mode_ = "disabled";
  /// @brief Stores a mapping of logical qubit names to physical qubit names.
  /// @details This mapping can be important for the execution of QIR programs.
  ///          Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 parameter.
  ///          This is transferred as a comma-separated list of pairs of
  ///          logical and physical qubit names, for example,
  ///              "alice:QB0,bob:QB1"
  std::optional<std::vector<std::pair<std::string, std::string>>>
      qubit_mapping_ = std::nullopt;
  /// @brief The maximum duration of a circuit over T2 time.
  /// @details Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM5+1 parameter.
  std::optional<double> max_circuit_duration_over_t2_ = std::nullopt;
  /// @brief The number of active reset cycles.
  /// @details Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM5+2 parameter.
  std::optional<size_t> num_active_reset_cycles_ = std::nullopt;
  /// @brief The dynamical decoupling strategy to use for the job.
  /// @details Can be set via the QDMI_DEVICE_JOB_PARAMETER_CUSTOM5+3 parameter.
  ///          This is transferred as a JSON string according to the data model
  ///          defined at
  ///          https://github.com/iqm-finland/sdk/blob/1a563651751bb0779026fcc7f45d8ca676c365c3/iqm_client/src/iqm/iqm_client/models.py#L791
  std::optional<std::string> dd_strategy_ = std::nullopt;
  /// The dictionary of results (histogram counts).
  std::map<std::string, size_t> counts_;
  /// Individual shot measurement results as bitstrings. std::nullopt means not
  /// yet fetched, empty vector means fetched but no shots.
  std::optional<std::vector<std::string>> shots_;
  /// The new calibration set ID after a successful calibration.
  std::string new_calibration_set_id_;
  /// The status of the job.
  QDMI_Job_Status status_ = QDMI_JOB_STATUS_CREATED;
};

/**
 * @brief Implementation of the IQM_QDMI_Device_Site structure.
 */
struct IQM_QDMI_Site_impl_d {
  std::string name_;
  size_t id_{};
  uint64_t t1_{};
  uint64_t t2_{};
};

/**
 * @brief Implementation of the IQM_QDMI_Device_Operation structure.
 */
struct IQM_QDMI_Operation_impl_d {
  std::string name_;
  std::string implementation_;
};

// NOLINTBEGIN(bugprone-macro-parentheses)
#define ADD_SINGLE_VALUE_PROPERTY(prop_name, prop_type, prop_value, prop,      \
                                  size, value, size_ret)                       \
  {                                                                            \
    if ((prop) == (prop_name)) {                                               \
      if ((value) != nullptr) {                                                \
        if ((size) < sizeof(prop_type)) {                                      \
          return QDMI_ERROR_INVALIDARGUMENT;                                   \
        }                                                                      \
        *static_cast<prop_type *>(value) = prop_value;                         \
      }                                                                        \
      if ((size_ret) != nullptr) {                                             \
        *size_ret = sizeof(prop_type);                                         \
      }                                                                        \
      return QDMI_SUCCESS;                                                     \
    }                                                                          \
  }

#define ADD_STRING_PROPERTY(prop_name, prop_value, prop, size, value,          \
                            size_ret)                                          \
  {                                                                            \
    if ((prop) == (prop_name)) {                                               \
      if ((value) != nullptr) {                                                \
        if ((size) < strlen(prop_value) + 1) {                                 \
          return QDMI_ERROR_INVALIDARGUMENT;                                   \
        }                                                                      \
        strncpy(static_cast<char *>(value), prop_value, size);                 \
        static_cast<char *>(value)[size - 1] = '\0';                           \
      }                                                                        \
      if ((size_ret) != nullptr) {                                             \
        *size_ret = strlen(prop_value) + 1;                                    \
      }                                                                        \
      return QDMI_SUCCESS;                                                     \
    }                                                                          \
  }

#define ADD_LIST_PROPERTY(prop_name, prop_type, prop_values, prop, size,       \
                          value, size_ret)                                     \
  {                                                                            \
    if ((prop) == (prop_name)) {                                               \
      if ((value) != nullptr) {                                                \
        if ((size) < (prop_values).size() * sizeof(prop_type)) {               \
          return QDMI_ERROR_INVALIDARGUMENT;                                   \
        }                                                                      \
        memcpy(static_cast<void *>(value),                                     \
               static_cast<const void *>((prop_values).data()),                \
               (prop_values).size() * sizeof(prop_type));                      \
      }                                                                        \
      if ((size_ret) != nullptr) {                                             \
        *size_ret = (prop_values).size() * sizeof(prop_type);                  \
      }                                                                        \
      return QDMI_SUCCESS;                                                     \
    }                                                                          \
  }
// NOLINTEND(bugprone-macro-parentheses)

int IQM_QDMI_device_initialize() { return QDMI_SUCCESS; }

int IQM_QDMI_device_finalize() { return QDMI_SUCCESS; }

int IQM_QDMI_device_session_alloc(IQM_QDMI_Device_Session *session) {
  if (session == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  *session = new IQM_QDMI_Device_Session_impl_d();
  LOG_INFO("Allocated new device session");
  return QDMI_SUCCESS;
}

namespace {
int Process_static_quantum_architecture(IQM_QDMI_Device_Session session) {
  LOG_INFO("Processing static quantum architecture");

  const auto qc_list_url =
      session->api_config_->url(iqm::API_ENDPOINT::GET_QUANTUM_COMPUTERS);
  std::string qc_list_response;
  const auto &bearer_token = session->token_manager_->get_bearer_token();
  auto status =
      session->http_client_->get(qc_list_url, bearer_token, qc_list_response);
  if (status != QDMI_SUCCESS) {
    return status;
  }
  const auto json_response =
      nlohmann::json::parse(qc_list_response); // NOLINT(misc-include-cleaner)

  // Extract the quantum_computers array from the response
  if (!json_response.contains("quantum_computers") ||
      !json_response["quantum_computers"].is_array() ||
      json_response["quantum_computers"].empty()) {
    LOG_ERROR("No quantum computers available or invalid response format");
    return QDMI_ERROR_FATAL;
  }

  const auto &json_qc_list = json_response["quantum_computers"];

  // Select quantum computer: use `quantum_computer_id_` or
  // `quantum_computer_alias_` if set, otherwise use first available
  if (session->quantum_computer_id_.has_value()) {
    const std::string &qc_id = session->quantum_computer_id_.value();
    // Search for the specified quantum computer by ID
    bool found = false;
    for (const auto &qc : json_qc_list) {
      if (qc["id"].get<std::string>() == qc_id) {
        found = true;
        session->quantum_computer_alias_ = qc["alias"].get<std::string>();
        break;
      }
    }
    if (!found) {
      LOG_ERROR("Quantum computer ID '" + qc_id + "' not found");
      return QDMI_ERROR_FATAL;
    }
  } else if (session->quantum_computer_alias_.has_value()) {
    const std::string &qc_alias = session->quantum_computer_alias_.value();
    // Search for the specified quantum computer by alias
    bool found = false;
    for (const auto &qc : json_qc_list) {
      if (qc["alias"].get<std::string>() == qc_alias) {
        session->quantum_computer_id_ = qc["id"].get<std::string>();
        found = true;
        break;
      }
    }
    if (!found) {
      LOG_ERROR("Quantum computer alias '" + qc_alias + "' not found");
      return QDMI_ERROR_FATAL;
    }
  } else {
    // Use first available quantum computer
    const auto &qc = json_qc_list[0];
    session->quantum_computer_id_ = qc["id"].get<std::string>();
    session->quantum_computer_alias_ = qc["alias"].get<std::string>();
  }
  LOG_INFO("Using quantum computer ID: " + *session->quantum_computer_id_);
  LOG_INFO("Using quantum computer alias: " +
           *session->quantum_computer_alias_);

  const auto arch_url = session->api_config_->url(
      iqm::API_ENDPOINT::GET_STATIC_QUANTUM_ARCHITECTURE,
      *session->quantum_computer_alias_);
  std::string arch_response;
  status = session->http_client_->get(arch_url, bearer_token, arch_response);
  if (status != QDMI_SUCCESS) {
    return status;
  }
  const auto arch_array =
      nlohmann::json::parse(arch_response); // NOLINT(misc-include-cleaner)

  // Extract first element from array (API returns array with single object)
  if (!arch_array.is_array() || arch_array.empty()) {
    LOG_ERROR("Invalid static quantum architecture response format");
    return QDMI_ERROR_FATAL;
  }
  const auto &architecture = arch_array[0];

  const auto &qubits = architecture["qubits"];
  const auto num_qubits = qubits.size();
  LOG_INFO("Found " + std::to_string(num_qubits) + " qubits");
  session->sites_.reserve(num_qubits);
  session->sites_map_.reserve(num_qubits);
  for (size_t i = 0; i < num_qubits; ++i) {
    auto &site =
        session->sites_.emplace_back(std::make_unique<IQM_QDMI_Site_impl_d>());
    site->name_ = qubits[i].get<std::string>();
    site->id_ = i;
    session->sites_ptr_.emplace_back(site.get());
    session->sites_map_[site->name_] = site.get();
  }

  const auto &connectivity = architecture["connectivity"];
  const auto num_edges = connectivity.size();
  LOG_INFO("Found " + std::to_string(num_edges) + " connectivity edges");
  session->connectivity_.reserve(num_edges * 2);
  for (size_t i = 0; i < num_edges; ++i) {
    const auto &edge = connectivity[i];
    const auto &site1 = session->sites_map_[edge[0].get<std::string>()];
    const auto &site2 = session->sites_map_[edge[1].get<std::string>()];
    session->connectivity_.emplace_back(site1, site2);
    session->connectivity_.emplace_back(site2, site1);
  }
  return QDMI_SUCCESS;
}

int Process_calibrated_gates(IQM_QDMI_Device_Session session) {
  LOG_INFO("Processing calibrated gates");
  const auto &bearer_token = session->token_manager_->get_bearer_token();
  const auto dyn_arch_url = session->api_config_->url(
      iqm::API_ENDPOINT::GET_DYNAMIC_QUANTUM_ARCHITECTURE,
      *session->quantum_computer_alias_, session->calibration_set_id_);
  std::string dyn_arch_response;
  const auto status =
      session->http_client_->get(dyn_arch_url, bearer_token, dyn_arch_response);
  if (status != QDMI_SUCCESS) {
    return status;
  }
  const auto dynamic_architecture = nlohmann::json::parse(dyn_arch_response);

  session->calibration_set_id_ =
      dynamic_architecture["calibration_set_id"].get<std::string>();
  LOG_INFO("Using calibration set ID: " + session->calibration_set_id_);

  const auto &gates = dynamic_architecture["gates"];
  session->operations_.reserve(gates.size());
  session->operations_map_.reserve(gates.size());
  for (const auto &[gate_name, gate_details] : gates.items()) {
    if (gate_name == "cc_prx" || gate_name == "reset_wait" ||
        gate_name == "measure_fidelity") {
      LOG_DEBUG("Skipping unsupported gate: " + gate_name);
      continue; // Skip unsupported gates
    }
    LOG_DEBUG("Processing gate: " + gate_name);
    auto &operation = session->operations_.emplace_back(
        std::make_unique<IQM_QDMI_Operation_impl_d>());
    operation->name_ = gate_name;
    session->operations_ptr_.emplace_back(operation.get());
    session->operations_map_[gate_name] = operation.get();

    const auto default_implementation = gate_details["default_implementation"];
    operation->implementation_ = default_implementation.get<std::string>();
    const auto &qubit_lists =
        gate_details["implementations"][default_implementation]["loci"];
    auto &operation_qubit_lists =
        session->operations_sites_map_[operation.get()];
    operation_qubit_lists.reserve(qubit_lists.size());
    for (const auto &qubit_list : qubit_lists) {
      auto &qubit_list_vec = operation_qubit_lists.emplace_back();
      qubit_list_vec.reserve(qubit_list.size());
      for (const auto &qubit : qubit_list) {
        const auto &site = session->sites_map_[qubit.get<std::string>()];
        qubit_list_vec.emplace_back(site);
      }
    }
  }
  return QDMI_SUCCESS;
}

int Process_calibration_metrics(IQM_QDMI_Device_Session session) {
  LOG_INFO(
      "Processing calibration set quality metrics for calibration set ID: " +
      session->calibration_set_id_);
  const auto calibration_url = session->api_config_->url(
      iqm::API_ENDPOINT::GET_CALIBRATION_SET_QUALITY_METRICS,
      *session->quantum_computer_alias_, session->calibration_set_id_);
  std::string calibration_response;
  const auto &bearer_token = session->token_manager_->get_bearer_token();
  const auto status = session->http_client_->get(calibration_url, bearer_token,
                                                 calibration_response);
  if (status != QDMI_SUCCESS) {
    return status;
  }
  const auto calibration_json_response =
      nlohmann::json::parse(calibration_response);

  const auto &observations = calibration_json_response["observations"];
  auto metrics = std::unordered_map<std::string, double>{};
  for (const auto &observation : observations) {
    if (observation["invalid"].get<bool>()) {
      continue;
    }
    const auto &dut_field = observation["dut_field"].get<std::string>();
    const auto value = observation["value"].get<double>();
    metrics[dut_field] = value;
  }

  for (const auto &qubit : session->sites_) {
    const std::string t1_key =
        "characterization.model." + qubit->name_ + ".t1_time";
    if (const auto t1_value = metrics.find(t1_key); t1_value != metrics.end()) {
      // convert from seconds to us
      qubit->t1_ = static_cast<uint64_t>(t1_value->second * 1e6);
    }
    const std::string t2_key =
        "characterization.model." + qubit->name_ + ".t2_time";
    if (const auto t2_value = metrics.find(t2_key); t2_value != metrics.end()) {
      // convert from seconds to us
      qubit->t2_ = static_cast<uint64_t>(t2_value->second * 1e6);
    }
  }

  for (const auto &operation : session->operations_) {
    const auto &name = operation->name_;
    const auto &qubit_lists = session->operations_sites_map_[operation.get()];
    if (name == "measure") {
      auto &single_qubit_fidelity_map =
          session->operations_single_qubit_fidelity_map_[operation.get()];
      const std::string measure_key =
          "metrics.ssro.measure." + operation->implementation_ + ".";
      single_qubit_fidelity_map.reserve(qubit_lists.size());
      for (const auto &qubit_list : qubit_lists) {
        assert(qubit_list.size() == 1);
        const auto &qubit = qubit_list[0];
        const auto measure_qubit_key = measure_key + qubit->name_ + ".fidelity";
        if (const auto metric = metrics.find(measure_qubit_key);
            metric != metrics.end()) {
          single_qubit_fidelity_map[qubit] = metric->second;
        }
      }
    }
    if (name == "prx") {
      auto &single_qubit_fidelity_map =
          session->operations_single_qubit_fidelity_map_[operation.get()];
      const std::string prx_key =
          "metrics.rb.prx." + operation->implementation_ + ".";
      single_qubit_fidelity_map.reserve(qubit_lists.size());
      for (const auto &qubit_list : qubit_lists) {
        assert(qubit_list.size() == 1);
        const auto &qubit = qubit_list[0];
        const auto measure_qubit_key =
            prx_key + qubit->name_ + ".fidelity:par=d2";
        if (const auto metric = metrics.find(measure_qubit_key);
            metric != metrics.end()) {
          single_qubit_fidelity_map[qubit] = metric->second;
        }
      }
    }
    if (name == "cz") {
      auto &two_qubit_fidelity_map =
          session->operations_two_qubit_fidelity_map_[operation.get()];
      const std::string cz_key =
          "metrics.irb.cz." + operation->implementation_ + ".";
      two_qubit_fidelity_map.reserve(qubit_lists.size());
      for (const auto &qubit_list : qubit_lists) {
        assert(qubit_list.size() == 2);
        const auto &qubit1 = qubit_list[0];
        const auto &qubit2 = qubit_list[1];
        const auto cz_qubit_key =
            cz_key + qubit1->name_ + "__" + qubit2->name_ + ".fidelity:par=d2";
        if (const auto metric = metrics.find(cz_qubit_key);
            metric != metrics.end()) {
          two_qubit_fidelity_map[std::make_pair(qubit1, qubit2)] =
              metric->second;
        }
      }
    }
  }
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_update_dynamic_quantum_architecture(
    IQM_QDMI_Device_Session session,
    const std::string &calibration_set_id = "default") {
  if (session == nullptr || calibration_set_id.empty()) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  LOG_INFO("Updating dynamic quantum architecture with calibration set ID: " +
           calibration_set_id);
  session->calibration_set_id_ = calibration_set_id;
  session->operations_.clear();
  session->operations_ptr_.clear();
  session->operations_map_.clear();
  session->operations_sites_map_.clear();

  if (const auto ret = Process_calibrated_gates(session); ret != QDMI_SUCCESS) {
    return ret;
  }

  // Get the latest quality metrics
  if (const auto ret = Process_calibration_metrics(session);
      ret != QDMI_SUCCESS) {
    return ret;
  }

  return QDMI_SUCCESS;
}
} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage)
IQM_QDMI_EXPORT int IQM_QDMI_device_session_init_with_http_client(
    IQM_QDMI_Device_Session session,
    std::unique_ptr<iqm::IHttpClient> http_client) {
  if (session == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }

  // Check if session is already initialized
  if (session->session_status_ == IQM_QDMI_DEVICE_SESSION_STATUS::INITIALIZED) {
    LOG_ERROR("Session is already initialized");
    return QDMI_ERROR_BADSTATE;
  }

  LOG_INFO("Initializing device session");
  if (session->base_url_.empty()) {
    LOG_ERROR("Base URL not set");
    return QDMI_ERROR_FATAL;
  }
  session->api_config_ = std::make_unique<iqm::APIConfig>(session->base_url_);

  session->http_client_ = std::move(http_client);

  session->token_manager_ = std::make_unique<iqm::TokenManager>(
      session->token_, session->tokens_file_);
  session->session_status_ = IQM_QDMI_DEVICE_SESSION_STATUS::INITIALIZED;
  session->device_status_ = QDMI_DEVICE_STATUS_IDLE;

  // Get the static quantum architecture via a curl GET request
  if (const auto ret = Process_static_quantum_architecture(session);
      ret != QDMI_SUCCESS) {
    return ret;
  }

  // Get the dynamic quantum architecture via a curl GET request
  if (const auto ret =
          IQM_QDMI_device_update_dynamic_quantum_architecture(session);
      ret != QDMI_SUCCESS) {
    return ret;
  }

  LOG_INFO("Checking whether calibration jobs are supported");
  const auto cocos_health_url =
      session->api_config_->url(iqm::API_ENDPOINT::COCOS_HEALTH);
  std::string cocos_health_response;
  const auto status = session->http_client_->get(
      cocos_health_url, session->token_manager_->get_bearer_token(),
      cocos_health_response);
  session->supports_calibration_jobs_ = (status == QDMI_SUCCESS);

  LOG_INFO("Device session initialized successfully");
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_session_init(IQM_QDMI_Device_Session session) {
  return IQM_QDMI_device_session_init_with_http_client(
      session, std::make_unique<iqm::CurlHttpClient>());
}

void IQM_QDMI_device_session_free(IQM_QDMI_Device_Session session) {
  if (session == nullptr) {
    return;
  }
  LOG_INFO("Freeing device session");
  delete session;
}

int IQM_QDMI_device_session_set_parameter(IQM_QDMI_Device_Session session,
                                          QDMI_Device_Session_Parameter param,
                                          const size_t size,
                                          const void *value) {
  if (session == nullptr || (value != nullptr && size == 0) ||
      (param >= QDMI_DEVICE_SESSION_PARAMETER_MAX &&
       param != QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1 &&
       param != QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2 &&
       param != QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3 &&
       param != QDMI_DEVICE_SESSION_PARAMETER_CUSTOM4 &&
       param != QDMI_DEVICE_SESSION_PARAMETER_CUSTOM5)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (session->session_status_ != IQM_QDMI_DEVICE_SESSION_STATUS::ALLOCATED) {
    LOG_ERROR("Setting session parameter in wrong state");
    return QDMI_ERROR_BADSTATE;
  }
  std::optional<std::string> *value_str = nullptr;
  switch (param) {
  case QDMI_DEVICE_SESSION_PARAMETER_BASEURL:
    if (value != nullptr) {
      session->base_url_ =
          std::string(static_cast<const char *>(value), size - 1);
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_SESSION_PARAMETER_TOKEN:
    value_str = &session->token_;
    break;
  case QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE:
    value_str = &session->tokens_file_;
    break;
  case QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1:
    value_str = &session->quantum_computer_id_;
    break;
  case QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2:
    value_str = &session->quantum_computer_alias_;
    break;
  default:
    return QDMI_ERROR_NOTSUPPORTED;
  }
  if (value != nullptr) {
    *value_str = std::string(static_cast<const char *>(value), size - 1);
  }
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_session_create_device_job(IQM_QDMI_Device_Session session,
                                              IQM_QDMI_Device_Job *job) {
  if (session == nullptr || job == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (session->session_status_ != IQM_QDMI_DEVICE_SESSION_STATUS::INITIALIZED) {
    LOG_ERROR("Creating job in wrong state");
    return QDMI_ERROR_BADSTATE;
  }

  *job = new IQM_QDMI_Device_Job_impl_d;
  (*job)->session_ = session;
  (*job)->status_ = QDMI_JOB_STATUS_CREATED;
  LOG_INFO("Created new device job");
  return QDMI_SUCCESS;
}

void IQM_QDMI_device_job_free(IQM_QDMI_Device_Job job) {
  LOG_INFO("Freeing device job");
  delete[] static_cast<char *>(job->program_);
  delete job;
}

int IQM_QDMI_device_job_set_parameter(IQM_QDMI_Device_Job job,
                                      const QDMI_Device_Job_Parameter param,
                                      const size_t size, const void *value) {
  if (job == nullptr || (value != nullptr && size == 0) ||
      (param >= QDMI_DEVICE_JOB_PARAMETER_MAX &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM1 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM2 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM3 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM4 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 1 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2 &&
       param != QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 3)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (job->status_ != QDMI_JOB_STATUS_CREATED) {
    LOG_ERROR("Setting job parameter in wrong state");
    return QDMI_ERROR_BADSTATE;
  }
  switch (param) {
  case QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT:
    if (value != nullptr) {
      const auto format = *static_cast<const QDMI_Program_Format *>(value);
      if (format >= QDMI_PROGRAM_FORMAT_MAX &&
          format != QDMI_PROGRAM_FORMAT_CUSTOM1 &&
          format != QDMI_PROGRAM_FORMAT_CUSTOM2 &&
          format != QDMI_PROGRAM_FORMAT_CUSTOM3 &&
          format != QDMI_PROGRAM_FORMAT_CUSTOM4 &&
          format != QDMI_PROGRAM_FORMAT_CUSTOM5) {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      if (format == QDMI_PROGRAM_FORMAT_IQMJSON ||
          format == QDMI_PROGRAM_FORMAT_QIRBASESTRING ||
          (job->session_->supports_calibration_jobs_ &&
           format == QDMI_PROGRAM_FORMAT_CALIBRATION)) {
        job->program_format_ = format;
        return QDMI_SUCCESS;
      }
      return QDMI_ERROR_NOTSUPPORTED;
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_PROGRAM:
    if (value != nullptr) {
      job->program_ = new char[size];
      memcpy(job->program_, value, size);
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM:
    if (value != nullptr) {
      job->num_shots_ = *static_cast<const size_t *>(value);
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_CUSTOM1:
    if (value != nullptr) {
      const std::string heralding_mode(static_cast<const char *>(value));
      if (heralding_mode != "none" && heralding_mode != "zeros") {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      job->heralding_mode_ = heralding_mode;
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_CUSTOM2:
    if (value != nullptr) {
      const std::string move_validation_mode(static_cast<const char *>(value));
      if (move_validation_mode != "strict" &&
          move_validation_mode != "allow_prx" &&
          move_validation_mode != "none") {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      job->move_validation_mode_ = move_validation_mode;
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_CUSTOM3:
    if (value != nullptr) {
      const std::string move_gate_frame_tracking_mode(
          static_cast<const char *>(value));
      if (move_gate_frame_tracking_mode != "full" &&
          move_gate_frame_tracking_mode != "no_detuning_correction" &&
          move_gate_frame_tracking_mode != "none") {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      job->move_gate_frame_tracking_mode_ = move_gate_frame_tracking_mode;
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_CUSTOM4:
    if (value != nullptr) {
      const std::string dynamic_decoupling_mode(
          static_cast<const char *>(value));
      if (dynamic_decoupling_mode != "disabled" &&
          dynamic_decoupling_mode != "enabled") {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      job->dd_mode_ = dynamic_decoupling_mode;
    }
    return QDMI_SUCCESS;
  case QDMI_DEVICE_JOB_PARAMETER_CUSTOM5:
    if (value != nullptr) {
      std::string qubit_mapping_str(static_cast<const char *>(value));
      std::vector<std::pair<std::string, std::string>> qubit_mapping;
      size_t pos = 0;
      while ((pos = qubit_mapping_str.find(',')) != std::string::npos) {
        const auto pair = qubit_mapping_str.substr(0, pos);
        const auto colon_pos = pair.find(':');
        if (colon_pos == std::string::npos) {
          return QDMI_ERROR_INVALIDARGUMENT;
        }
        qubit_mapping.emplace_back(pair.substr(0, colon_pos),
                                   pair.substr(colon_pos + 1));
        qubit_mapping_str.erase(0, pos + 1);
      }
      if (!qubit_mapping_str.empty()) {
        const auto colon_pos = qubit_mapping_str.find(':');
        if (colon_pos == std::string::npos) {
          return QDMI_ERROR_INVALIDARGUMENT;
        }
        qubit_mapping.emplace_back(qubit_mapping_str.substr(0, colon_pos),
                                   qubit_mapping_str.substr(colon_pos + 1));
      }
      job->qubit_mapping_ = qubit_mapping;
    }
    return QDMI_SUCCESS;
  default:
    if (static_cast<int>(param) == QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 1) {
      if (value != nullptr) {
        job->max_circuit_duration_over_t2_ =
            *static_cast<const double *>(value);
      }
      return QDMI_SUCCESS;
    }
    if (static_cast<int>(param) == QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2) {
      if (value != nullptr) {
        job->num_active_reset_cycles_ = *static_cast<const size_t *>(value);
      }
      return QDMI_SUCCESS;
    }
    if (static_cast<int>(param) == QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 3) {
      if (value != nullptr) {
        job->dd_strategy_ = std::string(static_cast<const char *>(value));
      }
      return QDMI_SUCCESS;
    }
    return QDMI_ERROR_NOTSUPPORTED;
  }
}

int IQM_QDMI_device_job_query_property(IQM_QDMI_Device_Job job,
                                       QDMI_Device_Job_Property prop,
                                       const size_t size, void *value,
                                       size_t *size_ret) {
  if (job == nullptr || (value != nullptr && size == 0) ||
      (prop >= QDMI_DEVICE_JOB_PROPERTY_MAX &&
       prop != QDMI_DEVICE_JOB_PROPERTY_CUSTOM1 &&
       prop != QDMI_DEVICE_JOB_PROPERTY_CUSTOM2 &&
       prop != QDMI_DEVICE_JOB_PROPERTY_CUSTOM3 &&
       prop != QDMI_DEVICE_JOB_PROPERTY_CUSTOM4 &&
       prop != QDMI_DEVICE_JOB_PROPERTY_CUSTOM5)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  ADD_STRING_PROPERTY(QDMI_DEVICE_JOB_PROPERTY_ID, job->job_id_.c_str(), prop,
                      size, value, size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_JOB_PROPERTY_PROGRAMFORMAT,
                            QDMI_Program_Format, job->program_format_, prop,
                            size, value, size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_JOB_PROPERTY_PROGRAM, void *,
                            job->program_, prop, size, value, size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_JOB_PROPERTY_SHOTSNUM, size_t,
                            job->num_shots_, prop, size, value, size_ret)
  return QDMI_ERROR_NOTSUPPORTED;
}

namespace {
int IQM_QDMI_device_job_submit_circuit(IQM_QDMI_Device_Job job) {
  LOG_INFO("Submitting circuit job");
  auto json_program = nlohmann::json();
  json_program["circuits"] = nlohmann::json::array();
  if (job->program_format_ == QDMI_PROGRAM_FORMAT_IQMJSON) {
    json_program["circuits"].emplace_back(
        nlohmann::json::parse(static_cast<const char *>(job->program_)));
  } else {
    json_program["circuits"].emplace_back(
        std::string{static_cast<const char *>(job->program_)});
  }
  json_program["calibration_set_id"] = job->session_->calibration_set_id_;
  json_program["shots"] = job->num_shots_;
  json_program["heralding_mode"] = job->heralding_mode_;
  json_program["move_validation_mode"] = job->move_validation_mode_;
  json_program["move_gate_frame_tracking_mode"] =
      job->move_gate_frame_tracking_mode_;
  json_program["dd_mode"] = job->dd_mode_;
  if (job->qubit_mapping_) {
    nlohmann::json qubit_mapping_json = nlohmann::json::array();
    for (const auto &[logical, physical] : *job->qubit_mapping_) {
      qubit_mapping_json.push_back(
          {{"logical_name", logical}, {"physical_name", physical}});
    }
    json_program["qubit_mapping"] = qubit_mapping_json;
  }
  if (job->max_circuit_duration_over_t2_) {
    json_program["max_circuit_duration_over_t2"] =
        *job->max_circuit_duration_over_t2_;
  }
  if (job->num_active_reset_cycles_) {
    json_program["num_active_reset_cycles"] = *job->num_active_reset_cycles_;
  }
  if (job->dd_strategy_) {
    json_program["dd_strategy"] = nlohmann::json::parse(*job->dd_strategy_);
  }

  const auto job_submission_url =
      job->session_->api_config_->url(iqm::API_ENDPOINT::SUBMIT_CIRCUIT_JOB,
                                      *job->session_->quantum_computer_alias_);
  std::string job_submission_response;
  const auto status = job->session_->http_client_->post(
      job_submission_url, job->session_->token_manager_->get_bearer_token(),
      job_submission_response, json_program.dump(), "Expect: 100-continue");
  if (status != QDMI_SUCCESS) {
    job->status_ = QDMI_JOB_STATUS_FAILED;
    return QDMI_ERROR_FATAL;
  }
  const auto job_submission_json_response =
      nlohmann::json::parse(job_submission_response);

  job->job_id_ = job_submission_json_response["id"];
  job->status_ = QDMI_JOB_STATUS_SUBMITTED;

  // Log queue position if available
  std::string log_message = "Submitted job with ID: " + job->job_id_;
  if (job_submission_json_response.contains("queue_position")) {
    const auto &queue_position_json =
        job_submission_json_response["queue_position"];
    if (queue_position_json.is_number_integer()) {
      const auto queue_position = queue_position_json.get<int>();
      log_message +=
          " (queue position: " + std::to_string(queue_position) + ")";
    }
  }
  LOG_INFO(log_message);

  return QDMI_SUCCESS;
}

int IQM_QDMI_device_job_submit_calibration(IQM_QDMI_Device_Job job) {
  if (!job->session_->supports_calibration_jobs_) {
    LOG_ERROR("Calibration jobs are not supported by this device");
    return QDMI_ERROR_NOTSUPPORTED;
  }
  LOG_INFO("Submitting calibration job");
  const auto job_submission_url = job->session_->api_config_->url(
      iqm::API_ENDPOINT::SUBMIT_CALIBRATION_JOB);
  std::string job_submission_response;
  const auto status = job->session_->http_client_->post(
      job_submission_url, job->session_->token_manager_->get_bearer_token(),
      job_submission_response, static_cast<const char *>(job->program_),
      "Expect: 100-continue");
  if (status != QDMI_SUCCESS) {
    job->status_ = QDMI_JOB_STATUS_FAILED;
    return QDMI_ERROR_FATAL;
  }
  const auto job_submission_json_response =
      nlohmann::json::parse(job_submission_response);

  job->job_id_ = job_submission_json_response["id"];
  job->status_ = QDMI_JOB_STATUS_SUBMITTED;

  // Log queue position if available
  std::string log_message =
      "Submitted calibration job with ID: " + job->job_id_;
  if (job_submission_json_response.contains("queue_position")) {
    const auto &queue_position_json =
        job_submission_json_response["queue_position"];
    if (queue_position_json.is_number_integer()) {
      const auto queue_position = queue_position_json.get<int>();
      log_message +=
          " (queue position: " + std::to_string(queue_position) + ")";
    }
  }
  LOG_INFO(log_message);

  return QDMI_SUCCESS;
}
} // namespace

int IQM_QDMI_device_job_submit(IQM_QDMI_Device_Job job) {
  if (job == nullptr || job->status_ != QDMI_JOB_STATUS_CREATED) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (job->session_ == nullptr) {
    return QDMI_ERROR_BADSTATE;
  }
  if (job->program_ == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  job->session_->device_status_ = QDMI_DEVICE_STATUS_BUSY;

  if (job->program_format_ == QDMI_PROGRAM_FORMAT_IQMJSON ||
      job->program_format_ == QDMI_PROGRAM_FORMAT_QIRBASESTRING) {
    return IQM_QDMI_device_job_submit_circuit(job);
  }
  if (job->program_format_ == QDMI_PROGRAM_FORMAT_CALIBRATION) {
    return IQM_QDMI_device_job_submit_calibration(job);
  }
  return QDMI_ERROR_INVALIDARGUMENT; // Unreachable; just a safety check
}

int IQM_QDMI_device_job_cancel(IQM_QDMI_Device_Job job) {
  if (job == nullptr || job->status_ == QDMI_JOB_STATUS_DONE) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (job->job_id_.empty()) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  LOG_INFO("Canceling job with ID: " + job->job_id_);
  const auto job_abortion_url =
      job->program_format_ == QDMI_PROGRAM_FORMAT_CALIBRATION
          ? job->session_->api_config_->url(
                iqm::API_ENDPOINT::ABORT_CALIBRATION_JOB, job->job_id_)
          : job->session_->api_config_->url(iqm::API_ENDPOINT::CANCEL_JOB,
                                            job->job_id_);
  std::string job_abortion_response;
  const auto status = job->session_->http_client_->post(
      job_abortion_url, job->session_->token_manager_->get_bearer_token(),
      job_abortion_response, "", "");
  if (status != QDMI_SUCCESS) {
    job->status_ = QDMI_JOB_STATUS_FAILED;
    return QDMI_ERROR_FATAL;
  }
  job->status_ = QDMI_JOB_STATUS_CANCELED;
  LOG_INFO("Job with ID: " + job->job_id_ + " canceled");
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_job_check(IQM_QDMI_Device_Job job,
                              QDMI_Job_Status *status) {
  if (job == nullptr || status == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (job->job_id_.empty() || job->status_ == QDMI_JOB_STATUS_DONE ||
      job->status_ == QDMI_JOB_STATUS_CANCELED) {
    *status = job->status_;
    return QDMI_SUCCESS;
  }
  LOG_DEBUG("Checking status for job with ID: " + job->job_id_);
  const auto job_status_url =
      job->program_format_ == QDMI_PROGRAM_FORMAT_CALIBRATION
          ? job->session_->api_config_->url(
                iqm::API_ENDPOINT::GET_CALIBRATION_JOB_STATUS, job->job_id_)
          : job->session_->api_config_->url(iqm::API_ENDPOINT::GET_JOB_STATUS,
                                            job->job_id_);
  std::string job_status_response;
  const auto status_code = job->session_->http_client_->get(
      job_status_url, job->session_->token_manager_->get_bearer_token(),
      job_status_response);
  if (status_code != QDMI_SUCCESS) {
    job->status_ = QDMI_JOB_STATUS_FAILED;
    return QDMI_ERROR_FATAL;
  }
  const auto job_status_json_response =
      nlohmann::json::parse(job_status_response);

  const auto job_status = job_status_json_response["status"].get<std::string>();
  if (job_status == "received") {
    job->status_ = QDMI_JOB_STATUS_SUBMITTED;
  } else if (job_status == "queued" || job_status == "waiting") {
    job->status_ = QDMI_JOB_STATUS_QUEUED;
  } else if (job_status == "validation_started" ||
             job_status == "validation_ended" ||
             job_status == "fetch_calibration_started" ||
             job_status == "fetch_calibration_ended" ||
             job_status == "compilation_started" ||
             job_status == "compilation_ended" ||
             job_status == "save_sweep_metadata_started" ||
             job_status == "save_sweep_metadata_ended" ||
             job_status == "pending execution" ||
             job_status == "pending_execution" ||
             job_status == "execution_started" ||
             job_status == "execution_ended" ||
             job_status == "post_processing_pending" ||
             job_status == "post_processing_started" ||
             job_status == "post_processing_ended" ||
             // Legacy job statuses
             job_status == "running" || job_status == "processing" ||
             job_status == "accepted" || job_status == "pending compilation" ||
             job_status == "compiled") {
    job->status_ = QDMI_JOB_STATUS_RUNNING;
  } else if (job_status == "ready" || job_status == "completed") {
    job->status_ = QDMI_JOB_STATUS_DONE;
  } else if (job_status == "aborted" || job_status == "cancelled") {
    job->status_ = QDMI_JOB_STATUS_CANCELED;
  } else if (job_status == "failed") {
    job->status_ = QDMI_JOB_STATUS_FAILED;
  } else {
    LOG_ERROR("Unknown job status: " + job_status);
    return QDMI_ERROR_FATAL;
  }
  *status = job->status_;
  LOG_DEBUG("Job status: " + std::to_string(job->status_) +
            " (native status: " + job_status + ")");
  return QDMI_SUCCESS;
}

namespace {
bool IQM_QDMI_device_job_done(IQM_QDMI_Device_Job job) {
  assert(job != nullptr);
  return job->status_ == QDMI_JOB_STATUS_DONE ||
         job->status_ == QDMI_JOB_STATUS_CANCELED ||
         job->status_ == QDMI_JOB_STATUS_FAILED;
}
} // namespace

int IQM_QDMI_device_job_wait(IQM_QDMI_Device_Job job, const size_t timeout) {
  if (job == nullptr) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (job->job_id_.empty()) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  LOG_INFO("Waiting for job " + job->job_id_ + " to complete");
  QDMI_Job_Status status = job->status_;
  size_t sleep_duration = 1; // Start with 1 second
  constexpr size_t max_sleep_duration = 60;
  if (timeout > 0) {
    const auto start = std::chrono::steady_clock::now();
    while (static_cast<size_t>(std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start)
                                   .count()) < timeout) {
      if (const auto ret = IQM_QDMI_device_job_check(job, &status);
          ret != QDMI_SUCCESS) {
        return ret;
      }
      if (IQM_QDMI_device_job_done(job)) {
        LOG_INFO("Job " + job->job_id_ + " completed with status " +
                 std::to_string(status));
        return QDMI_SUCCESS;
      }
      std::this_thread::sleep_for(std::chrono::seconds(sleep_duration));
      // Exponential backoff: double the sleep duration, but cap it
      sleep_duration = std::min(sleep_duration * 2, max_sleep_duration);
    }
    LOG_ERROR("Job " + job->job_id_ + " timed out");
    return QDMI_ERROR_TIMEOUT;
  }
  while (!IQM_QDMI_device_job_done(job)) {
    if (const auto ret = IQM_QDMI_device_job_check(job, &status);
        ret != QDMI_SUCCESS) {
      return ret;
    }
    if (IQM_QDMI_device_job_done(job)) {
      LOG_INFO("Job " + job->job_id_ + " completed with status " +
               std::to_string(status));
      return QDMI_SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::seconds(sleep_duration));
    // Exponential backoff: double the sleep duration, but cap it
    sleep_duration = std::min(sleep_duration * 2, max_sleep_duration);
  }
  return QDMI_SUCCESS;
}

namespace {
int IQM_QDMI_device_job_get_results_hist(IQM_QDMI_Device_Job job,
                                         const QDMI_Job_Result result,
                                         const size_t size, void *data,
                                         size_t *size_ret) {
  // Fetch the remote results, if not already fetched
  if (job->counts_.empty()) {
    // Optimization: if we already have shots, compute counts from them
    if (job->shots_.has_value() && !job->shots_->empty()) {
      LOG_INFO("Computing histogram counts from shots for job " + job->job_id_);
      for (const auto &shot : *job->shots_) {
        job->counts_[shot]++;
      }
    } else {
      LOG_INFO("Fetching results for job " + job->job_id_);
      const auto job_results_url = job->session_->api_config_->url(
          iqm::API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENT_COUNTS, job->job_id_);
      std::string job_results_response;
      const auto status = job->session_->http_client_->get(
          job_results_url, job->session_->token_manager_->get_bearer_token(),
          job_results_response);
      if (status != QDMI_SUCCESS) {
        // Only mark the job as failed for truly fatal errors, but always
        // propagate the underlying status code to the caller.
        if (status == QDMI_ERROR_FATAL) {
          job->status_ = QDMI_JOB_STATUS_FAILED;
        }
        return status;
      }
      const auto job_results_json_response =
          nlohmann::json::parse(job_results_response);
      // Response is an array with a single object containing counts
      for (const auto &counts = job_results_json_response[0]["counts"];
           const auto &[bitstring, count] : counts.items()) {
        job->counts_[bitstring] = count.get<size_t>();
      }
    }
  }
  if (result == QDMI_JOB_RESULT_HIST_KEYS) {
    if (job->counts_.empty()) {
      constexpr size_t req_size = 1;
      if (size_ret != nullptr) {
        *size_ret = req_size;
      }
      if (data != nullptr) {
        if (size < req_size) {
          return QDMI_ERROR_INVALIDARGUMENT;
        }
        auto *data_ptr = static_cast<char *>(data);
        *data_ptr = '\0';
      }
    } else {
      const size_t bitstring_size = job->counts_.begin()->first.length();
      const size_t req_size = job->counts_.size() * (bitstring_size + 1);
      if (size_ret != nullptr) {
        *size_ret = req_size;
      }
      if (data != nullptr) {
        if (size < req_size) {
          return QDMI_ERROR_INVALIDARGUMENT;
        }
        auto *data_ptr = static_cast<char *>(data);
        for (const auto &bitstring : job->counts_ | std::views::keys) {
          std::ranges::copy(bitstring, data_ptr);
          data_ptr += bitstring.length();
          *data_ptr++ = ',';
        }
        *(data_ptr - 1) = '\0'; // Replace last comma with null terminator
      }
    }
  } else {
    // case QDMI_JOB_RESULT_HIST_VALUES:
    const size_t req_size = job->counts_.size() * sizeof(size_t);
    if (size_ret != nullptr) {
      *size_ret = req_size;
    }
    if (data != nullptr) {
      if (size < req_size) {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      auto *data_ptr = static_cast<size_t *>(data);
      for (const auto &count : job->counts_ | std::views::values) {
        *data_ptr++ = count;
      }
    }
  }
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_job_get_results_calibration_id(IQM_QDMI_Device_Job job,
                                                   const size_t size,
                                                   void *data,
                                                   size_t *size_ret) {
  // Fetch the remote results, if not already fetched
  if (job->new_calibration_set_id_.empty()) {
    LOG_INFO("Fetching calibration results for job " + job->job_id_);
    const auto job_calibration_url = job->session_->api_config_->url(
        iqm::API_ENDPOINT::GET_CALIBRATION_JOB_STATUS, job->job_id_);
    std::string job_calibration_response;
    const auto status = job->session_->http_client_->get(
        job_calibration_url, job->session_->token_manager_->get_bearer_token(),
        job_calibration_response);
    if (status != QDMI_SUCCESS) {
      // Only mark the job as failed for truly fatal errors, but always
      // propagate the underlying status code to the caller.
      if (status == QDMI_ERROR_FATAL) {
        job->status_ = QDMI_JOB_STATUS_FAILED;
      }
      return status;
    }
    const auto job_calibration_json_response =
        nlohmann::json::parse(job_calibration_response);
    if (!job_calibration_json_response["result"].contains("success") ||
        !job_calibration_json_response["result"]["success"].get<bool>()) {
      LOG_ERROR("Calibration job failed");
      job->status_ = QDMI_JOB_STATUS_FAILED;
      return QDMI_ERROR_FATAL;
    }
    job->new_calibration_set_id_ =
        job_calibration_json_response["result"]["calibration_set_id"]
            .get<std::string>();

    // Update the dynamic quantum architecture with the new calibration set ID
    auto ret = IQM_QDMI_device_update_dynamic_quantum_architecture(
        job->session_, job->new_calibration_set_id_);
    if (ret != QDMI_SUCCESS) {
      LOG_INFO("Failed to update dynamic quantum architecture after "
               "calibration request. Retrying in 120 seconds...");
      std::this_thread::sleep_for(std::chrono::seconds(120));
      ret = IQM_QDMI_device_update_dynamic_quantum_architecture(
          job->session_, job->new_calibration_set_id_);
      if (ret != QDMI_SUCCESS) {
        return ret;
      }
    }
  }

  const size_t req_size = job->new_calibration_set_id_.length() + 1;
  if (size_ret != nullptr) {
    *size_ret = req_size;
  }
  if (data != nullptr) {
    if (size < req_size) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    auto *data_ptr = static_cast<char *>(data);
    strncpy(data_ptr, job->new_calibration_set_id_.c_str(), size);
    data_ptr[size - 1] = '\0'; // Ensure null-termination
  }
  return QDMI_SUCCESS;
}

int IQM_QDMI_device_job_get_results_shots(IQM_QDMI_Device_Job job,
                                          const size_t size, void *data,
                                          size_t *size_ret) {
  // Fetch the remote results, if not already fetched
  if (!job->shots_.has_value()) {
    LOG_INFO("Fetching shot measurements for job " + job->job_id_);
    const auto job_measurements_url = job->session_->api_config_->url(
        iqm::API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENTS, job->job_id_);
    std::string job_measurements_response;
    const auto status = job->session_->http_client_->get(
        job_measurements_url, job->session_->token_manager_->get_bearer_token(),
        job_measurements_response);
    if (status != QDMI_SUCCESS) {
      // Only mark the job as failed for truly fatal errors, but always
      // propagate the underlying status code to the caller.
      if (status == QDMI_ERROR_FATAL) {
        job->status_ = QDMI_JOB_STATUS_FAILED;
      }
      return status;
    }

    const auto job_measurements_json_response =
        nlohmann::json::parse(job_measurements_response);

    // API returns array format: [{"meas_key": [[0], [1], ...], ...}, ...]
    // The outer array typically contains a single object.
    // Each measurement key maps to an array of shot results.
    // Each shot result is itself an array containing a single integer (0 or 1).
    if (!job_measurements_json_response.is_array()) {
      LOG_ERROR("Expected array of measurement objects for job " +
                job->job_id_);
      job->status_ = QDMI_JOB_STATUS_FAILED;
      return QDMI_ERROR_FATAL;
    }

    // Initialize shots_ with an empty vector (marking as "fetched")
    job->shots_ = std::vector<std::string>();

    // Empty array means no shots
    if (job_measurements_json_response.empty()) {
      LOG_INFO("No shot measurements for job " + job->job_id_);
    } else {
      for (const auto &measurement_obj : job_measurements_json_response) {
        if (!measurement_obj.is_object()) {
          LOG_ERROR("Expected object in measurements array for job " +
                    job->job_id_);
          job->status_ = QDMI_JOB_STATUS_FAILED;
          return QDMI_ERROR_FATAL;
        }
        if (measurement_obj.empty()) {
          LOG_ERROR("Expected non-empty measurement object for job " +
                    job->job_id_);
          job->status_ = QDMI_JOB_STATUS_FAILED;
          return QDMI_ERROR_FATAL;
        }

        // Collect and sort measurement keys for consistent bitstring ordering
        std::vector<std::string> keys;
        keys.reserve(measurement_obj.size());
        for (auto it = measurement_obj.begin(); it != measurement_obj.end();
             ++it) {
          keys.emplace_back(it.key());
        }
        std::ranges::sort(keys);

        // Determine the number of shots from the first key
        size_t num_shots = 0;
        if (!keys.empty()) {
          const auto &first_key_results = measurement_obj[keys[0]];
          if (!first_key_results.is_array()) {
            LOG_ERROR("Expected array for measurement key '" + keys[0] +
                      "' in job " + job->job_id_);
            job->status_ = QDMI_JOB_STATUS_FAILED;
            return QDMI_ERROR_FATAL;
          }
          num_shots = first_key_results.size();
        }

        // Validate that the number of shots matches what was requested
        if (num_shots != job->num_shots_) {
          LOG_ERROR(
              "Number of shots in measurement response (" +
              std::to_string(num_shots) + ") does not match requested shots (" +
              std::to_string(job->num_shots_) + ") for job " + job->job_id_);
          job->status_ = QDMI_JOB_STATUS_FAILED;
          return QDMI_ERROR_FATAL;
        }

        // Pre-allocate shots
        if (job->shots_->empty()) {
          job->shots_->resize(num_shots);
        }

        // For each measurement key, process all shots
        for (const auto &key : keys) {
          const auto &key_results = measurement_obj[key];
          if (!key_results.is_array()) {
            LOG_ERROR("Expected array for measurement key '" + key +
                      "' in job " + job->job_id_);
            job->status_ = QDMI_JOB_STATUS_FAILED;
            return QDMI_ERROR_FATAL;
          }

          if (key_results.size() != num_shots) {
            LOG_ERROR("Inconsistent number of shots for measurement key '" +
                      key + "': expected " + std::to_string(num_shots) +
                      ", got " + std::to_string(key_results.size()) +
                      " in job " + job->job_id_);
            job->status_ = QDMI_JOB_STATUS_FAILED;
            return QDMI_ERROR_FATAL;
          }

          // Process each shot for this measurement key
          for (size_t shot_idx = 0; shot_idx < num_shots; ++shot_idx) {
            const auto &qubit_result = key_results[shot_idx];
            if (!qubit_result.is_array() || qubit_result.empty()) {
              LOG_ERROR("Invalid qubit result format for measurement key '" +
                        key + "', shot " + std::to_string(shot_idx) +
                        " in job " + job->job_id_);
              job->status_ = QDMI_JOB_STATUS_FAILED;
              return QDMI_ERROR_FATAL;
            }

            // Each qubit result array must contain exactly one integer outcome
            if (qubit_result.size() != 1 ||
                !qubit_result[0].is_number_integer()) {
              LOG_ERROR("Invalid qubit result value format for measurement "
                        "key '" +
                        key + "', shot " + std::to_string(shot_idx) +
                        " in job " + job->job_id_);
              job->status_ = QDMI_JOB_STATUS_FAILED;
              return QDMI_ERROR_FATAL;
            }

            const auto value = qubit_result[0].get<int>();
            if (value < 0 || value > 1) {
              LOG_ERROR("Invalid qubit value " + std::to_string(value) +
                        " for measurement key '" + key + "', shot " +
                        std::to_string(shot_idx) + " in job " + job->job_id_);
              job->status_ = QDMI_JOB_STATUS_FAILED;
              return QDMI_ERROR_FATAL;
            }

            // Append to the bitstring for this shot
            (*job->shots_)[shot_idx] += std::to_string(value);
          }
        }
      }
    }
  }

  if (!job->shots_.has_value() || job->shots_->empty()) {
    LOG_INFO("No shot measurements for job " + job->job_id_);
    constexpr size_t req_size = 1;
    if (size_ret != nullptr) {
      *size_ret = req_size;
    }
    if (data != nullptr) {
      if (size < req_size) {
        return QDMI_ERROR_INVALIDARGUMENT;
      }
      auto *data_ptr = static_cast<char *>(data);
      *data_ptr = '\0';
    }
    return QDMI_SUCCESS;
  }

  // Calculate required size: bitstring lengths + commas + null terminator
  // All shots have the same bitstring length (number of qubits measured)
  const size_t bitstring_length = (*job->shots_)[0].length();
  const size_t total_bitstring_length = bitstring_length * job->shots_->size();
  // For N shots, we need (N - 1) commas and 1 null terminator => N extra
  // characters.
  const size_t req_size = total_bitstring_length + job->shots_->size();

  if (size_ret != nullptr) {
    *size_ret = req_size;
  }

  if (data != nullptr) {
    if (size < req_size) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }

    auto *data_ptr = static_cast<char *>(data);
    for (auto it = job->shots_->begin(); it != job->shots_->end(); ++it) {
      data_ptr = std::copy(it->begin(), it->end(), data_ptr);
      if (std::next(it) != job->shots_->end()) {
        *data_ptr++ = ','; // Add comma separator
      } else {
        *data_ptr++ = '\0'; // Add null terminator at the end
      }
    }
  }

  return QDMI_SUCCESS;
}
} // namespace

int IQM_QDMI_device_job_get_results(IQM_QDMI_Device_Job job,
                                    QDMI_Job_Result result, const size_t size,
                                    void *data, size_t *size_ret) {
  if (job == nullptr || (data != nullptr && size == 0) ||
      (result >= QDMI_JOB_RESULT_MAX && result != QDMI_JOB_RESULT_CUSTOM1 &&
       result != QDMI_JOB_RESULT_CUSTOM2 && result != QDMI_JOB_RESULT_CUSTOM3 &&
       result != QDMI_JOB_RESULT_CUSTOM4 &&
       result != QDMI_JOB_RESULT_CUSTOM5)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }

  switch (result) {
  case QDMI_JOB_RESULT_SHOTS:
    if (job->status_ != QDMI_JOB_STATUS_DONE) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    return IQM_QDMI_device_job_get_results_shots(job, size, data, size_ret);
  case QDMI_JOB_RESULT_HIST_KEYS:
  case QDMI_JOB_RESULT_HIST_VALUES:
    if (job->status_ != QDMI_JOB_STATUS_DONE) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    return IQM_QDMI_device_job_get_results_hist(job, result, size, data,
                                                size_ret);
  case QDMI_JOB_RESULT_CUSTOM1:
    if (job->status_ != QDMI_JOB_STATUS_DONE) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    // Custom result 1 is reserved for the calibration set ID
    return IQM_QDMI_device_job_get_results_calibration_id(job, size, data,
                                                          size_ret);
  default:
    return QDMI_ERROR_NOTSUPPORTED;
  }
}

namespace {
constexpr std::array SUPPORTED_PROGRAM_FORMATS = {
    QDMI_PROGRAM_FORMAT_QIRBASESTRING, QDMI_PROGRAM_FORMAT_IQMJSON};
constexpr std::array SUPPORTED_PROGRAM_FORMATS_WITH_CALIBRATION = {
    QDMI_PROGRAM_FORMAT_QIRBASESTRING, QDMI_PROGRAM_FORMAT_IQMJSON,
    QDMI_PROGRAM_FORMAT_CALIBRATION};
} // namespace

int IQM_QDMI_device_session_query_device_property(
    IQM_QDMI_Device_Session session, const QDMI_Device_Property prop,
    const size_t size, void *value, size_t *size_ret) {
  if (session == nullptr || (value != nullptr && size == 0) ||
      (prop >= QDMI_DEVICE_PROPERTY_MAX &&
       prop != QDMI_DEVICE_PROPERTY_CUSTOM1 &&
       prop != QDMI_DEVICE_PROPERTY_CUSTOM2 &&
       prop != QDMI_DEVICE_PROPERTY_CUSTOM3 &&
       prop != QDMI_DEVICE_PROPERTY_CUSTOM4 &&
       prop != QDMI_DEVICE_PROPERTY_CUSTOM5)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  if (session->session_status_ != IQM_QDMI_DEVICE_SESSION_STATUS::INITIALIZED) {
    return QDMI_ERROR_BADSTATE;
  }
  ADD_STRING_PROPERTY(QDMI_DEVICE_PROPERTY_NAME, "IQM QDMI Device", prop, size,
                      value, size_ret)
  // NOLINTNEXTLINE(misc-include-cleaner)
  ADD_STRING_PROPERTY(QDMI_DEVICE_PROPERTY_VERSION, IQM_QDMI_DEVICE_VERSION,
                      prop, size, value, size_ret)
  // NOLINTNEXTLINE(misc-include-cleaner)
  ADD_STRING_PROPERTY(QDMI_DEVICE_PROPERTY_LIBRARYVERSION, QDMI_VERSION, prop,
                      size, value, size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_PROPERTY_STATUS, QDMI_Device_Status,
                            session->device_status_, prop, size, value,
                            size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_PROPERTY_QUBITSNUM, size_t,
                            session->sites_.size(), prop, size, value, size_ret)
  ADD_LIST_PROPERTY(QDMI_DEVICE_PROPERTY_SITES, IQM_QDMI_Site,
                    session->sites_ptr_, prop, size, value, size_ret)
  ADD_LIST_PROPERTY(QDMI_DEVICE_PROPERTY_OPERATIONS, IQM_QDMI_Operation,
                    session->operations_ptr_, prop, size, value, size_ret)
  ADD_LIST_PROPERTY(QDMI_DEVICE_PROPERTY_COUPLINGMAP,
                    (std::pair<IQM_QDMI_Site, IQM_QDMI_Site>{}),
                    session->connectivity_, prop, size, value, size_ret)
  ADD_STRING_PROPERTY(QDMI_DEVICE_PROPERTY_DURATIONUNIT, "us", prop, size,
                      value, size_ret)
  ADD_SINGLE_VALUE_PROPERTY(QDMI_DEVICE_PROPERTY_DURATIONSCALEFACTOR, double,
                            1.0, prop, size, value, size_ret)
  // While QDMI does not properly support expose pulse-level properties, the
  // corresponding property will simply be set to NONE.
  ADD_SINGLE_VALUE_PROPERTY(
      QDMI_DEVICE_PROPERTY_PULSESUPPORT, QDMI_Device_Pulse_Support_Level,
      QDMI_DEVICE_PULSE_SUPPORT_LEVEL_NONE, prop, size, value, size_ret)
  if (session->supports_calibration_jobs_) {
    ADD_LIST_PROPERTY(
        QDMI_DEVICE_PROPERTY_SUPPORTEDPROGRAMFORMATS, QDMI_Program_Format,
        SUPPORTED_PROGRAM_FORMATS_WITH_CALIBRATION, prop, size, value, size_ret)
  } else {
    ADD_LIST_PROPERTY(QDMI_DEVICE_PROPERTY_SUPPORTEDPROGRAMFORMATS,
                      QDMI_Program_Format, SUPPORTED_PROGRAM_FORMATS, prop,
                      size, value, size_ret)
  }
  return QDMI_ERROR_NOTSUPPORTED;
}

int IQM_QDMI_device_session_query_site_property(IQM_QDMI_Device_Session session,
                                                IQM_QDMI_Site site,
                                                const QDMI_Site_Property prop,
                                                const size_t size, void *value,
                                                size_t *size_ret) {
  if (session == nullptr || site == nullptr ||
      (value != nullptr && size == 0) ||
      (prop >= QDMI_SITE_PROPERTY_MAX && prop != QDMI_SITE_PROPERTY_CUSTOM1 &&
       prop != QDMI_SITE_PROPERTY_CUSTOM2 &&
       prop != QDMI_SITE_PROPERTY_CUSTOM3 &&
       prop != QDMI_SITE_PROPERTY_CUSTOM4 &&
       prop != QDMI_SITE_PROPERTY_CUSTOM5)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  ADD_SINGLE_VALUE_PROPERTY(QDMI_SITE_PROPERTY_INDEX, uint64_t, site->id_, prop,
                            size, value, size_ret)
  if (site->t1_ > 0) {
    ADD_SINGLE_VALUE_PROPERTY(QDMI_SITE_PROPERTY_T1, uint64_t, site->t1_, prop,
                              size, value, size_ret)
  }
  if (site->t2_ > 0) {
    ADD_SINGLE_VALUE_PROPERTY(QDMI_SITE_PROPERTY_T2, uint64_t, site->t2_, prop,
                              size, value, size_ret)
  }
  ADD_STRING_PROPERTY(QDMI_SITE_PROPERTY_NAME, site->name_.c_str(), prop, size,
                      value, size_ret)
  return QDMI_ERROR_NOTSUPPORTED;
}

int IQM_QDMI_device_session_query_operation_property(
    IQM_QDMI_Device_Session session, IQM_QDMI_Operation operation,
    const size_t num_sites, const IQM_QDMI_Site *sites, const size_t num_params,
    const double *params, const QDMI_Operation_Property prop, const size_t size,
    void *value, size_t *size_ret) {
  if (session == nullptr || operation == nullptr ||
      (sites != nullptr && num_sites == 0) ||
      (params != nullptr && num_params == 0) ||
      (value != nullptr && size == 0) ||
      (prop >= QDMI_OPERATION_PROPERTY_MAX &&
       prop != QDMI_OPERATION_PROPERTY_CUSTOM1 &&
       prop != QDMI_OPERATION_PROPERTY_CUSTOM2 &&
       prop != QDMI_OPERATION_PROPERTY_CUSTOM3 &&
       prop != QDMI_OPERATION_PROPERTY_CUSTOM4 &&
       prop != QDMI_OPERATION_PROPERTY_CUSTOM5) ||
      !session->operations_sites_map_.contains(operation)) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }
  // General properties
  ADD_STRING_PROPERTY(QDMI_OPERATION_PROPERTY_NAME, operation->name_.c_str(),
                      prop, size, value, size_ret)

  const auto &available_sites_for_op =
      session->operations_sites_map_.at(operation);
  assert(!available_sites_for_op.empty());
  const auto num_op_sites = available_sites_for_op.front().size();
  ADD_SINGLE_VALUE_PROPERTY(QDMI_OPERATION_PROPERTY_QUBITSNUM, size_t,
                            num_op_sites, prop, size, value, size_ret)
  if (sites != nullptr && num_sites != num_op_sites) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }

  if (prop == QDMI_OPERATION_PROPERTY_SITES) {
    std::vector<IQM_QDMI_Site> sites_list;
    sites_list.reserve(num_op_sites * available_sites_for_op.size());
    for (const auto &site_group : available_sites_for_op) {
      sites_list.insert(sites_list.end(), site_group.begin(), site_group.end());
    }
    ADD_LIST_PROPERTY(QDMI_OPERATION_PROPERTY_SITES, IQM_QDMI_Site, sites_list,
                      prop, size, value, size_ret);
  }

  if (sites != nullptr) {
    // wrap the sites parameter in a vector
    auto sites_vec = std::vector<IQM_QDMI_Site>(sites, sites + num_sites);
    // need to find this vector in the list of available sites
    auto it = std::ranges::find(available_sites_for_op, sites_vec);
    if (it == available_sites_for_op.end() && num_op_sites == 2) {
      // try with reversed sites
      std::ranges::reverse(sites_vec);
      it = std::ranges::find(available_sites_for_op, sites_vec);
    }
    if (it == available_sites_for_op.end()) {
      return QDMI_ERROR_INVALIDARGUMENT;
    }
  }
  size_t num_op_params = 0;
  if (operation->name_ == "prx") {
    num_op_params = 2;
  } else if (operation->name_ == "prx_12") {
    num_op_params = 1;
  }
  ADD_SINGLE_VALUE_PROPERTY(QDMI_OPERATION_PROPERTY_PARAMETERSNUM, size_t,
                            num_op_params, prop, size, value, size_ret)
  if (params != nullptr && num_params != num_op_params) {
    return QDMI_ERROR_INVALIDARGUMENT;
  }

  if (num_op_sites == 1) {
    const auto it =
        session->operations_single_qubit_fidelity_map_.find(operation);
    if (it == session->operations_single_qubit_fidelity_map_.end()) {
      return QDMI_ERROR_NOTSUPPORTED;
    }
    auto &single_qubit_fidelity_map = it->second;
    if (sites != nullptr) {
      const auto fid_it = single_qubit_fidelity_map.find(sites[0]);
      if (fid_it == single_qubit_fidelity_map.end()) {
        return QDMI_ERROR_NOTSUPPORTED;
      }
      ADD_SINGLE_VALUE_PROPERTY(QDMI_OPERATION_PROPERTY_FIDELITY, double,
                                fid_it->second, prop, size, value, size_ret)
    }
  }
  if (num_op_sites == 2) {
    const auto it = session->operations_two_qubit_fidelity_map_.find(operation);
    if (it == session->operations_two_qubit_fidelity_map_.end()) {
      return QDMI_ERROR_NOTSUPPORTED;
    }
    auto &two_qubit_fidelity_map = it->second;
    if (sites != nullptr) {
      auto fid_it = two_qubit_fidelity_map.find({sites[0], sites[1]});
      if (fid_it == two_qubit_fidelity_map.end()) {
        fid_it = two_qubit_fidelity_map.find({sites[1], sites[0]});
      }
      if (fid_it == two_qubit_fidelity_map.end()) {
        return QDMI_ERROR_NOTSUPPORTED;
      }
      ADD_SINGLE_VALUE_PROPERTY(QDMI_OPERATION_PROPERTY_FIDELITY, double,
                                fid_it->second, prop, size, value, size_ret)
    }
  }

  return QDMI_ERROR_NOTSUPPORTED;
}
