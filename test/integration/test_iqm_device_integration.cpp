/*
 * Copyright (c) 2025 - 2026 IQM Finland Oy
 * All rights reserved.
 *
 * Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "fomac.hpp"
#include "iqm_qdmi/device.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Default timeout for job wait operations in integration tests (5 minutes)
// This prevents tests from hanging indefinitely while still allowing enough
// time for real job execution on the quantum computer.
constexpr size_t DEFAULT_JOB_WAIT_TIMEOUT = 300; // seconds

namespace {
class QDMIIntegrationTest : public testing::Test {
protected:
  IQM_QDMI_Device_Session session = nullptr;
  FoMaC fomac{};
  std::optional<std::string> requested_qc_alias = std::nullopt;

  void SetUp() override {
    EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
    const auto *token_env = std::getenv("IQM_TOKEN");
    const auto *tokens_file_env = std::getenv("IQM_TOKENS_FILE");
    if (token_env == nullptr && tokens_file_env == nullptr) {
      std::cerr << "[WARN] Neither IQM_TOKEN nor IQM_TOKENS_FILE is set. "
                   "Some tests may fail if authentication is required.\n";
    }
    const std::optional<std::string> token =
        (token_env != nullptr) ? std::make_optional<std::string>(token_env)
                               : std::nullopt;
    const std::optional<std::string> tokens_file =
        (tokens_file_env != nullptr)
            ? std::make_optional<std::string>(tokens_file_env)
            : std::nullopt;

    const auto *base_url_env = std::getenv("IQM_BASE_URL");
    const std::string base_url =
        (base_url_env != nullptr) ? base_url_env : "https://resonance.iqm.tech";

    const auto *qc_id_env = std::getenv("IQM_QC_ID");
    const std::optional<std::string> qc_id =
        (qc_id_env != nullptr) ? std::make_optional<std::string>(qc_id_env)
                               : std::nullopt;

    const auto *qc_alias_env = std::getenv("IQM_QC_ALIAS");
    const std::optional<std::string> qc_alias =
        (qc_alias_env != nullptr)
            ? std::make_optional<std::string>(qc_alias_env)
            : std::nullopt;
    requested_qc_alias = qc_alias;

    session =
        FoMaC::get_iqm_session(base_url, token, tokens_file, qc_id, qc_alias);
    fomac = FoMaC{session};
  }

  void TearDown() override {
    IQM_QDMI_device_session_free(session);
    EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
  }

  static auto is_qubit_site_name(const std::string &name) -> bool {
    return name.starts_with("QB");
  }

  [[nodiscard]] auto get_qubit_sites() const -> std::vector<IQM_QDMI_Site> {
    std::vector<IQM_QDMI_Site> qubit_sites;
    for (const auto &site : fomac.get_sites()) {
      if (is_qubit_site_name(fomac.get_site_name(site))) {
        qubit_sites.emplace_back(site);
      }
    }
    return qubit_sites;
  }

  [[nodiscard]] auto build_iqm_json_test_circuit() const -> std::string {
    const auto ops = fomac.get_operation_map();
    const auto prx_it = ops.find("prx");
    const auto measure_it = ops.find("measure");
    if (prx_it == ops.end() || measure_it == ops.end()) {
      throw std::runtime_error("Missing mandatory operations (prx/measure).");
    }

    const auto prx_sites = fomac.get_operation_sites(prx_it->second);
    const auto measure_sites = fomac.get_operation_sites(measure_it->second);
    std::vector<IQM_QDMI_Site> eligible_single_sites;
    for (const auto &site : prx_sites) {
      if (!is_qubit_site_name(fomac.get_site_name(site))) {
        continue;
      }
      if (std::ranges::find(measure_sites, site) != measure_sites.end()) {
        eligible_single_sites.emplace_back(site);
      }
    }
    if (eligible_single_sites.empty()) {
      throw std::runtime_error("No qubit site supports both prx and measure.");
    }

    auto *const q1 = eligible_single_sites.front();
    const auto q1_name = fomac.get_site_name(q1);

    auto build_single_qubit = [&] {
      std::ostringstream circuit;
      circuit << R"({"name":"test_circuit","instructions":[)";
      circuit << R"({"name":"prx","locus":[")" << q1_name
              << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
      circuit << R"({"name":"measure","locus":[")" << q1_name
              << R"("],"args":{"key":"meas_1_0_0"}})";
      circuit << R"(],"metadata":{}})";
      return circuit.str();
    };

    const auto cz_it = ops.find("cz");
    if (cz_it == ops.end()) {
      return build_single_qubit();
    }

    const auto cz_sites_flat = fomac.get_operation_sites(cz_it->second);
    std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>> cz_pairs;
    for (size_t i = 0; i + 1 < cz_sites_flat.size(); i += 2) {
      cz_pairs.emplace_back(cz_sites_flat[i], cz_sites_flat[i + 1]);
    }

    for (const auto &[a, b] : cz_pairs) {
      const auto a_name = fomac.get_site_name(a);
      const auto b_name = fomac.get_site_name(b);
      if (!is_qubit_site_name(a_name) || !is_qubit_site_name(b_name)) {
        continue;
      }
      if (std::ranges::find(prx_sites, a) == prx_sites.end() ||
          std::ranges::find(prx_sites, b) == prx_sites.end() ||
          std::ranges::find(measure_sites, a) == measure_sites.end() ||
          std::ranges::find(measure_sites, b) == measure_sites.end()) {
        continue;
      }

      std::ostringstream circuit;
      circuit << R"({"name":"test_circuit","instructions":[)";
      circuit << R"({"name":"prx","locus":[")" << a_name
              << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
      circuit << R"({"name":"prx","locus":[")" << b_name
              << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
      circuit << R"({"name":"cz","locus":[")" << a_name << R"(",")" << b_name
              << R"("],"args":{}},)";
      circuit << R"({"name":"prx","locus":[")" << b_name
              << R"("],"args":{"angle_t":0.25,"phase_t":1.25}},)";
      circuit << R"({"name":"measure","locus":[")" << a_name
              << R"("],"args":{"key":"meas_2_0_0"}},)";
      circuit << R"({"name":"measure","locus":[")" << b_name
              << R"("],"args":{"key":"meas_2_0_1"}})";
      circuit << R"(],"metadata":{}})";
      return circuit.str();
    }

    if (const auto move_it = ops.find("move"); move_it != ops.end()) {
      const auto move_sites_flat = fomac.get_operation_sites(move_it->second);
      std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>> move_pairs;
      for (size_t i = 0; i + 1 < move_sites_flat.size(); i += 2) {
        move_pairs.emplace_back(move_sites_flat[i], move_sites_flat[i + 1]);
      }

      for (const auto &[cz_first, cz_second] : cz_pairs) {
        IQM_QDMI_Site qubit = nullptr;
        IQM_QDMI_Site resonator = nullptr;
        const auto first_name = fomac.get_site_name(cz_first);
        const auto second_name = fomac.get_site_name(cz_second);
        if (is_qubit_site_name(first_name) &&
            !is_qubit_site_name(second_name)) {
          qubit = cz_first;
          resonator = cz_second;
        } else if (!is_qubit_site_name(first_name) &&
                   is_qubit_site_name(second_name)) {
          qubit = cz_second;
          resonator = cz_first;
        } else {
          continue;
        }
        if (std::ranges::find(prx_sites, qubit) == prx_sites.end() ||
            std::ranges::find(measure_sites, qubit) == measure_sites.end()) {
          continue;
        }

        for (const auto &[fst, snd] : move_pairs) {
          IQM_QDMI_Site second_qubit = nullptr;
          if (fst == resonator &&
              is_qubit_site_name(fomac.get_site_name(snd))) {
            second_qubit = snd;
          } else if (snd == resonator &&
                     is_qubit_site_name(fomac.get_site_name(fst))) {
            second_qubit = fst;
          }
          if (second_qubit == nullptr || second_qubit == qubit ||
              std::ranges::find(prx_sites, second_qubit) == prx_sites.end() ||
              std::ranges::find(measure_sites, second_qubit) ==
                  measure_sites.end()) {
            continue;
          }

          const auto qubit_name = fomac.get_site_name(qubit);
          const auto second_qubit_name = fomac.get_site_name(second_qubit);
          const auto resonator_name = fomac.get_site_name(resonator);
          std::ostringstream circuit;
          circuit << R"({"name":"test_circuit","instructions":[)";
          circuit << R"({"name":"prx","locus":[")" << qubit_name
                  << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
          circuit << R"({"name":"prx","locus":[")" << second_qubit_name
                  << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
          circuit << R"({"name":"move","locus":[")" << second_qubit_name
                  << R"(",")" << resonator_name << R"("],"args":{}},)";
          circuit << R"({"name":"cz","locus":[")" << qubit_name << R"(",")"
                  << resonator_name << R"("],"args":{}},)";
          circuit << R"({"name":"move","locus":[")" << second_qubit_name
                  << R"(",")" << resonator_name << R"("],"args":{}},)";
          circuit << R"({"name":"prx","locus":[")" << second_qubit_name
                  << R"("],"args":{"angle_t":0.25,"phase_t":1.25}},)";
          circuit << R"({"name":"measure","locus":[")" << qubit_name
                  << R"("],"args":{"key":"meas_2_0_0"}},)";
          circuit << R"({"name":"measure","locus":[")" << second_qubit_name
                  << R"("],"args":{"key":"meas_2_0_1"}})";
          circuit << R"(],"metadata":{}})";
          return circuit.str();
        }
      }
    }

    return build_single_qubit();
  }

  [[nodiscard]] static auto build_qir_test_circuit() -> std::string {
    return R"(; ModuleID = 'test_circuit'
source_filename = "test_circuit"

%Qubit = type opaque
%Result = type opaque

define void @test_circuit() #0 {
entry:
  call void @__quantum__rt__initialize(i8* null)
  call void @__quantum__qis__r__body(double 0x3FF921FB54442D1A, double 0x4012D97C7F3321D2, %Qubit* null)
  call void @__quantum__qis__mz__body(%Qubit* null, %Result* null)
  call void @__quantum__rt__array_record_output(i64 1, i8* null)
  call void @__quantum__rt__result_record_output(%Result* null, i8* null)
  ret void
}

declare void @__quantum__rt__initialize(i8*)

declare void @__quantum__qis__r__body(double, double, %Qubit*)

declare void @__quantum__qis__mz__body(%Qubit*, %Result* writeonly) #1

declare void @__quantum__rt__array_record_output(i64, i8*)

declare void @__quantum__rt__result_record_output(%Result*, i8*)

attributes #0 = { "entry_point" "output_labeling_schema" "qir_profiles"="custom" "required_num_qubits"="1" "required_num_results"="1" }
attributes #1 = { "irreversible" }

!llvm.module.flags = !{!0, !1, !2, !3}

!0 = !{i32 1, !"qir_major_version", i32 1}
!1 = !{i32 7, !"qir_minor_version", i32 0}
!2 = !{i32 1, !"dynamic_qubit_management", i1 false}
!3 = !{i32 1, !"dynamic_result_management", i1 false}
)";
  }

  static auto wait_for_done(IQM_QDMI_Device_Job job) -> QDMI_Job_Status {
    const auto wait_status =
        IQM_QDMI_device_job_wait(job, DEFAULT_JOB_WAIT_TIMEOUT);
    EXPECT_EQ(wait_status, QDMI_SUCCESS);

    QDMI_Job_Status status = QDMI_JOB_STATUS_CREATED;
    const auto check_status = IQM_QDMI_device_job_check(job, &status);
    EXPECT_EQ(check_status, QDMI_SUCCESS);
    return status;
  }

  [[nodiscard]] auto expects_mock_job_failures() const -> bool {
    return requested_qc_alias.has_value() &&
           *requested_qc_alias == "sirius:mock";
  }

  [[nodiscard]] auto
  should_skip_for_expected_mock_failure(const QDMI_Job_Status status) const
      -> bool {
    return status != QDMI_JOB_STATUS_DONE && expects_mock_job_failures();
  }

  static constexpr auto TEST_CALIBRATION_CONFIG = R"(
    {
      "procedure_config": {
        "excluded_qubits": [],
        "excluded_pairs": [],
        "initial_calibration_point": null,
        "remove_gbc_prefixed_observations": true,
        "set_default": "always",
        "description": "QDMI test",
      },
      "builder_config": {
        "name": "iqm.cocos.app.MockGraphBuilder",
        "args": {},
      },
      "graph_config": {},
      "graph_definition": null,
    })";
};
} // namespace

TEST_F(QDMIIntegrationTest, QueryDeviceProperties) {
  const auto device_name = fomac.get_name();
  ASSERT_FALSE(device_name.empty()) << "Device must provide a name";
  if (requested_qc_alias.has_value()) {
    EXPECT_EQ(device_name, *requested_qc_alias)
        << "Device name must match the selected QC alias";
  }

  const auto version = fomac.get_version();
  ASSERT_FALSE(version.empty()) << "Device must provide a version";

  const auto library_version = fomac.get_library_version();
  ASSERT_FALSE(library_version.empty())
      << "Device must provide a library version";

  ASSERT_GT(fomac.get_qubits_num(), 0);

  const auto gates = fomac.get_operation_map();
  ASSERT_GT(gates.size(), 0);
  for (const auto &[op_name, op] : gates) {
    ASSERT_FALSE(op_name.empty());
    const auto name = fomac.get_operation_name(op);
    EXPECT_EQ(op_name, name);
  }

  const auto coupling_map = fomac.get_coupling_map();
  if (const auto num_qubits = fomac.get_qubits_num(); num_qubits == 1) {
    ASSERT_TRUE(coupling_map.empty());
  } else {
    ASSERT_GT(coupling_map.size(), 0);
  }

  const auto duration_unit = fomac.get_duration_unit();
  ASSERT_FALSE(duration_unit.empty());

  const auto duration_scale_factor = fomac.get_duration_scale_factor();
  ASSERT_GT(duration_scale_factor, 0.0);

  const auto calibration_set_id = fomac.get_calibration_set_id();
  ASSERT_FALSE(calibration_set_id.empty())
      << "Device must provide a calibration set ID";

  // The MAX property is not a valid value for any device.
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_MAX, 0, nullptr, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  // Custom property 1 exposes the calibration set ID.
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_CUSTOM1, 0, nullptr, nullptr),
            QDMI_SUCCESS);
  // The IQM device does not support the remaining custom device properties.
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_CUSTOM2, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_CUSTOM3, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_CUSTOM4, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_CUSTOM5, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
}

TEST_F(QDMIIntegrationTest, QueuePropertiesOnGarnetMock) {
  if (!requested_qc_alias.has_value() || *requested_qc_alias != "garnet:mock") {
    GTEST_SKIP() << "Queue-property integration coverage targets garnet:mock";
  }

  size_t queue_length = 0;
  ASSERT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_SUCCESS);

  constexpr size_t shots_num = 64;
  constexpr size_t jobs_num = 4;
  constexpr size_t max_queue_position_queries = 10;
  constexpr auto query_interval = std::chrono::milliseconds{200};
  const auto circuit = build_iqm_json_test_circuit();
  std::vector<IQM_QDMI_Device_Job> jobs;
  jobs.reserve(jobs_num);
  for (size_t i = 0; i < jobs_num; ++i) {
    jobs.emplace_back(
        fomac.submit_job(circuit, QDMI_PROGRAM_FORMAT_IQMJSON, shots_num));
  }

  // Poll the final job for as long as it plausibly sits in Garnet's queue.
  // Every outcome here is legitimate: the property answers with a position
  // while the job is queued, QDMI_ERROR_NOTSUPPORTED while the queue has not
  // published one yet, and QDMI_ERROR_BADSTATE once the job has left the queue.
  // What the poll asserts is that no other code ever comes back.
  std::optional<size_t> observed_queue_position;
  bool job_left_the_queue = false;
  for (size_t i = 0; i < max_queue_position_queries; ++i) {
    size_t queue_position = 0;
    const auto result = IQM_QDMI_device_job_query_property(
        jobs.back(), QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
        sizeof(queue_position), &queue_position, nullptr);
    if (result == QDMI_SUCCESS) {
      observed_queue_position = queue_position;
      break;
    }
    if (result == QDMI_ERROR_BADSTATE) {
      job_left_the_queue = true;
      break;
    }
    EXPECT_EQ(result, QDMI_ERROR_NOTSUPPORTED);
    if (result != QDMI_ERROR_NOTSUPPORTED) {
      break;
    }
    std::this_thread::sleep_for(query_interval);
  }

  // Garnet decides how long a job stays queued, and a mock job of this size can
  // run to completion before the first query returns. Requiring a position to
  // have been seen would assert on the timing of a shared service rather than
  // on this device. The unit tests pin the property's contract against the HTTP
  // stub, where it is deterministic.
  if (!observed_queue_position.has_value()) {
    GTEST_LOG_(INFO) << (job_left_the_queue
                             ? "The job left Garnet's queue before a position "
                               "could be observed"
                             : "Garnet published no queue position within the "
                               "polling window");
  }
  for (auto *job : jobs) {
    EXPECT_EQ(wait_for_done(job), QDMI_JOB_STATUS_DONE);
    IQM_QDMI_device_job_free(job);
  }
}

TEST_F(QDMIIntegrationTest, QuerySiteProperties) {
  // The site list covers the qubits and, on Star-topology devices, the
  // computational resonators as well, so it is at least as long as the qubit
  // count and longer whenever the device has resonators.
  const auto sites = fomac.get_sites();
  const auto qubits_num = fomac.get_qubits_num();
  EXPECT_GE(sites.size(), qubits_num);

  const auto duration_scale_factor = fomac.get_duration_scale_factor();
  ASSERT_GT(duration_scale_factor, 0.0);

  // Site IDs index the site list, so they are bounded by its size rather than
  // by the qubit count.
  for (const auto &site : sites) {
    const auto site_id = fomac.get_site_index(site);
    EXPECT_LT(site_id, sites.size());
    const auto site_name = fomac.get_site_name(site);
    EXPECT_FALSE(site_name.empty());

    // T1 and T2 properties are optional
    try {
      const auto t1 =
          static_cast<double>(fomac.get_site_t1(site)) * duration_scale_factor;
      EXPECT_GT(t1, 0.0) << "T1 should be positive when available";
    } catch (const std::runtime_error &) {
      // T1 property is not supported - this is acceptable
      GTEST_LOG_(INFO) << "T1 property not supported for site " + site_name;
    }

    try {
      const auto t2 =
          static_cast<double>(fomac.get_site_t2(site)) * duration_scale_factor;
      EXPECT_GT(t2, 0.0) << "T2 should be positive when available";
    } catch (const std::runtime_error &) {
      // T2 property is not supported - this is acceptable
      GTEST_LOG_(INFO) << "T2 property not supported for site " + site_name;
    }

    // The MAX property is not a valid value for any device.
    EXPECT_EQ(IQM_QDMI_device_session_query_site_property(
                  session, site, QDMI_SITE_PROPERTY_MAX, 0, nullptr, nullptr),
              QDMI_ERROR_INVALIDARGUMENT);

    // The IQM device does not support custom properties for sites.
    EXPECT_EQ(
        IQM_QDMI_device_session_query_site_property(
            session, site, QDMI_SITE_PROPERTY_CUSTOM1, 0, nullptr, nullptr),
        QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(
        IQM_QDMI_device_session_query_site_property(
            session, site, QDMI_SITE_PROPERTY_CUSTOM2, 0, nullptr, nullptr),
        QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(
        IQM_QDMI_device_session_query_site_property(
            session, site, QDMI_SITE_PROPERTY_CUSTOM3, 0, nullptr, nullptr),
        QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(
        IQM_QDMI_device_session_query_site_property(
            session, site, QDMI_SITE_PROPERTY_CUSTOM4, 0, nullptr, nullptr),
        QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(
        IQM_QDMI_device_session_query_site_property(
            session, site, QDMI_SITE_PROPERTY_CUSTOM5, 0, nullptr, nullptr),
        QDMI_ERROR_NOTSUPPORTED);
  }
}

TEST_F(QDMIIntegrationTest, QueryGatePropertiesForEachGate) {
  const auto ops = fomac.get_operation_map();
  const auto coupling_map = fomac.get_coupling_map();

  for (const auto &op : ops | std::views::values) {
    const auto gate_name = fomac.get_operation_name(op);
    const auto gate_num_qubits = fomac.get_operation_operands_num(op);
    const auto gate_num_params = fomac.get_operation_parameters_num(op);

    if (gate_name == "prx_12") {
      continue; // The mock device does not expose calibration data for this
                // gate
    }

    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution dis(0.0, 1.0);
    std::vector<double> params(gate_num_params);
    for (auto &param : params) {
      param = dis(gen);
    }

    // Query the supported sites for the operation
    const auto supported_sites = fomac.get_operation_sites(op);
    EXPECT_FALSE(supported_sites.empty())
        << "Operation " + gate_name + " must support at least one site set";

    if (gate_num_qubits == 1) {
      for (const auto &site : supported_sites) {
        // Fidelity property is optional
        try {
          const auto fidelity =
              fomac.get_operation_fidelity(op, {site}, params);
          EXPECT_GE(fidelity, 0.0)
              << "Fidelity should be between 0 and 1 when available";
          EXPECT_LE(fidelity, 1.0)
              << "Fidelity should be between 0 and 1 when available";
        } catch (const std::invalid_argument &) {
          GTEST_LOG_(INFO) << "Fidelity property not available for operation " +
                                  gate_name + " on site " +
                                  fomac.get_site_name(site);
        } catch (const std::runtime_error &) {
          // Fidelity property is not supported - this is acceptable
          try {
            GTEST_LOG_(INFO)
                << "Fidelity property not supported for operation " +
                       gate_name + " on site " + fomac.get_site_name(site);
          } catch (...) {
            // If we can't get site names, just log without them
            GTEST_LOG_(INFO)
                << "Fidelity property not supported for operation " + gate_name;
          }
        }

        // Duration property is optional
        try {
          const auto duration =
              fomac.get_operation_duration(op, {site}, params);
          EXPECT_GT(duration, 0.0)
              << "Duration should be positive when available";
        } catch (const std::invalid_argument &) {
          GTEST_LOG_(INFO) << "Duration property not available for operation " +
                                  gate_name + " on site " +
                                  fomac.get_site_name(site);
        } catch (const std::runtime_error &) {
          // Duration property is not supported - this is acceptable
          try {
            GTEST_LOG_(INFO)
                << "Duration property not supported for operation " +
                       gate_name + " on site " + fomac.get_site_name(site);
          } catch (...) {
            // If we can't get site names, just log without them
            GTEST_LOG_(INFO)
                << "Duration property not supported for operation " + gate_name;
          }
        }
      }
    }
    if (gate_num_qubits == 2) {
      // Form site pairs out of the supported sites by always taking successive
      // pairs
      std::vector<std::pair<IQM_QDMI_Site, IQM_QDMI_Site>> site_pairs;
      for (size_t i = 0; i + 1 < supported_sites.size(); i += 2) {
        site_pairs.emplace_back(supported_sites[i], supported_sites[i + 1]);
      }
      // Each site pair should be part of the coupling map
      for (const auto &[control, target] : site_pairs) {
        const auto it = std::ranges::find_if(
            coupling_map, [control, target](const auto &pair) {
              return pair.first == control && pair.second == target;
            });
        if (it == coupling_map.end()) {
          // Try to get site names for better error message
          try {
            FAIL() << "Operation " + gate_name + " supports site pair (" +
                          fomac.get_site_name(control) + ", " +
                          fomac.get_site_name(target) +
                          ") which is not in the coupling map";
          } catch (...) {
            // If we can't get site names, fail without them
            FAIL()
                << "Operation " + gate_name +
                       " supports a site pair which is not in the coupling map";
          }
        }
      }

      for (const auto &[control, target] : coupling_map) {
        // Check if this coupling map pair is in the operation's supported sites
        const bool is_supported = std::ranges::any_of(
            site_pairs, [control, target](const auto &pair) {
              return pair.first == control && pair.second == target;
            });

        // Only query properties if the operation supports this pair
        if (!is_supported) {
          continue;
        }

        // Fidelity property is optional
        try {
          const auto fidelity =
              fomac.get_operation_fidelity(op, {control, target}, params);
          EXPECT_GE(fidelity, 0.0)
              << "Fidelity should be between 0 and 1 when available";
          EXPECT_LE(fidelity, 1.0)
              << "Fidelity should be between 0 and 1 when available";
        } catch (const std::invalid_argument &e) {
          FAIL() << "Invalid argument when querying fidelity for operation " +
                        gate_name + ": " + e.what();
        } catch (const std::runtime_error &) {
          // Fidelity property is not supported - this is acceptable
          try {
            GTEST_LOG_(INFO)
                << "Fidelity property not supported for operation " +
                       gate_name + " on sites " + fomac.get_site_name(control) +
                       ", " + fomac.get_site_name(target);
          } catch (...) {
            // If we can't get site names, just log without them
            GTEST_LOG_(INFO)
                << "Fidelity property not supported for operation " + gate_name;
          }
        } catch (const std::exception &e) {
          // Catch any other standard exception
          FAIL()
              << "Unexpected exception when querying fidelity for operation " +
                     gate_name + ": " + e.what();
        } catch (...) {
          // Catch everything else
          FAIL() << "Unknown exception when querying fidelity for operation " +
                        gate_name;
        }

        // Duration property is optional
        try {
          const auto duration =
              fomac.get_operation_duration(op, {control, target}, params);
          EXPECT_GT(duration, 0.0)
              << "Duration should be positive when available";
        } catch (const std::invalid_argument &e) {
          FAIL() << "Invalid argument when querying duration for operation " +
                        gate_name + ": " + e.what();
        } catch (const std::runtime_error &) {
          // Duration property is not supported - this is acceptable
          try {
            GTEST_LOG_(INFO)
                << "Duration property not supported for operation " +
                       gate_name + " on sites " + fomac.get_site_name(control) +
                       ", " + fomac.get_site_name(target);
          } catch (...) {
            // If we can't get site names, just log without them
            GTEST_LOG_(INFO)
                << "Duration property not supported for operation " + gate_name;
          }
        } catch (const std::exception &e) {
          // Catch any other standard exception
          FAIL()
              << "Unexpected exception when querying duration for operation " +
                     gate_name + ": " + e.what();
        } catch (...) {
          // Catch absolutely everything else
          FAIL() << "Unknown exception when querying duration for operation " +
                        gate_name;
        }
      }
    }

    // The MAX property is not a valid value for any device
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_MAX, 0, nullptr, nullptr),
              QDMI_ERROR_INVALIDARGUMENT);

    // The example devices do not support custom properties
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_CUSTOM1, 0, nullptr, nullptr),
              QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_CUSTOM2, 0, nullptr, nullptr),
              QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_CUSTOM3, 0, nullptr, nullptr),
              QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_CUSTOM4, 0, nullptr, nullptr),
              QDMI_ERROR_NOTSUPPORTED);
    EXPECT_EQ(IQM_QDMI_device_session_query_operation_property(
                  session, op, 0, nullptr, 0, nullptr,
                  QDMI_OPERATION_PROPERTY_CUSTOM5, 0, nullptr, nullptr),
              QDMI_ERROR_NOTSUPPORTED);
  }
}

TEST_F(QDMIIntegrationTest, JobCycle) {
  constexpr size_t shots_num = 64;
  const auto circuit = build_iqm_json_test_circuit();
  auto *job = fomac.submit_job(circuit, QDMI_PROGRAM_FORMAT_IQMJSON, shots_num);
  const auto job_id = FoMaC::get_job_id(job);
  const auto status = wait_for_done(job);
  if (should_skip_for_expected_mock_failure(status)) {
    IQM_QDMI_device_job_free(job);
    GTEST_SKIP()
        << "Skipping because sirius:mock currently fails job execution in "
           "backend (known mock limitation), observed status "
        << status;
  }
  ASSERT_EQ(status, QDMI_JOB_STATUS_DONE);

  const auto counts = FoMaC::get_histogram(job);

  size_t sum = 0;
  std::cout << "Counts: {\n";
  for (const auto &[key, count] : counts) {
    sum += count;
    std::cout << key << ": " << count << ",\n";
  }
  std::cout << "}\n";
  ASSERT_EQ(sum, shots_num);

  size_t size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &size),
            QDMI_SUCCESS);
  std::vector<char> buffer(size - 1); // Buffer too small
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                            buffer.size(), buffer.data(),
                                            nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  size_t size2 = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES, 0,
                                            nullptr, &size2),
            QDMI_SUCCESS);
  std::vector<char> buffer2(size2 - 1); // Buffer too small
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                            buffer2.size(), buffer2.data(),
                                            nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  // The MAX parameter is not a valid value for any device
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_MAX, 0,
                                            nullptr, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_SUCCESS);
  // shots_size > 1 means there's actual data beyond the null terminator
  if (shots_size > 1) {
    std::vector<char> shots_buffer(shots_size);
    EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                              shots_size, shots_buffer.data(),
                                              nullptr),
              QDMI_SUCCESS);
    const std::string shots_str(shots_buffer.data());
    EXPECT_FALSE(shots_str.empty());
  }

  // The IQM device does not support statevector or probability results
  EXPECT_EQ(IQM_QDMI_device_job_get_results(
                job, QDMI_JOB_RESULT_STATEVECTOR_DENSE, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(
      IQM_QDMI_device_job_get_results(
          job, QDMI_JOB_RESULT_STATEVECTOR_SPARSE_KEYS, 0, nullptr, nullptr),
      QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(
      IQM_QDMI_device_job_get_results(
          job, QDMI_JOB_RESULT_STATEVECTOR_SPARSE_VALUES, 0, nullptr, nullptr),
      QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(
                job, QDMI_JOB_RESULT_PROBABILITIES_DENSE, 0, nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(
      IQM_QDMI_device_job_get_results(
          job, QDMI_JOB_RESULT_PROBABILITIES_SPARSE_KEYS, 0, nullptr, nullptr),
      QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(
                job, QDMI_JOB_RESULT_PROBABILITIES_SPARSE_VALUES, 0, nullptr,
                nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM3, 0,
                                            nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM4, 0,
                                            nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM5, 0,
                                            nullptr, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  IQM_QDMI_device_job_free(job);

  IQM_QDMI_Device_Job retrieved_job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, job_id.c_str(), &retrieved_job),
            QDMI_SUCCESS);
  EXPECT_EQ(FoMaC::get_job_id(retrieved_job), job_id);
  EXPECT_EQ(IQM_QDMI_device_job_submit(retrieved_job), QDMI_ERROR_BADSTATE);
  EXPECT_EQ(wait_for_done(retrieved_job), QDMI_JOB_STATUS_DONE);
  const auto retrieved_counts = FoMaC::get_histogram(retrieved_job);
  const auto retrieved_sum =
      std::accumulate(retrieved_counts.begin(), retrieved_counts.end(),
                      size_t{0}, [](const size_t total, const auto &entry) {
                        return total + entry.second;
                      });
  EXPECT_EQ(retrieved_sum, shots_num);

  size_t retrieved_shots_size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(retrieved_job,
                                            QDMI_JOB_RESULT_SHOTS, 0, nullptr,
                                            &retrieved_shots_size),
            QDMI_SUCCESS);
  ASSERT_GT(retrieved_shots_size, 1U);
  std::vector<char> retrieved_shots(retrieved_shots_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(
                retrieved_job, QDMI_JOB_RESULT_SHOTS, retrieved_shots.size(),
                retrieved_shots.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(static_cast<size_t>(std::ranges::count(retrieved_shots, ',')) + 1,
            shots_num);
  IQM_QDMI_device_job_free(retrieved_job);
}

TEST_F(QDMIIntegrationTest, JobCancellation) {
  constexpr size_t shots_num = 64;
  const auto circuit = build_iqm_json_test_circuit();
  auto *job = fomac.submit_job(circuit, QDMI_PROGRAM_FORMAT_IQMJSON, shots_num);

  // A mock job of this size can reach a terminal status before the cancellation
  // request lands, and a job that is no longer running can no longer be
  // canceled. Which of the two happens is the backend's timing rather than this
  // device's behavior, so assert against whichever one occurred.
  const auto cancellation = IQM_QDMI_device_job_cancel(job);
  QDMI_Job_Status status{};
  ASSERT_EQ(IQM_QDMI_device_job_check(job, &status), QDMI_SUCCESS);
  if (cancellation == QDMI_SUCCESS) {
    EXPECT_EQ(status, QDMI_JOB_STATUS_CANCELED);
  } else {
    EXPECT_EQ(cancellation, QDMI_ERROR_INVALIDARGUMENT)
        << "A cancellation can only be refused because the job already reached "
           "a terminal status";
    EXPECT_TRUE(status == QDMI_JOB_STATUS_DONE ||
                status == QDMI_JOB_STATUS_CANCELED ||
                status == QDMI_JOB_STATUS_FAILED)
        << "The cancellation was refused, so the job must have finished; its "
           "status is "
        << status;
  }
  IQM_QDMI_device_job_free(job);
}

TEST_F(QDMIIntegrationTest, JobCycleCornerCases) {
  IQM_QDMI_Device_Job job{};
  EXPECT_EQ(IQM_QDMI_device_session_create_device_job(session, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_device_session_create_device_job(nullptr, &job),
            QDMI_ERROR_INVALIDARGUMENT);
  ASSERT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
            QDMI_SUCCESS);

  // The MAX parameter is not a valid program format for any device
  QDMI_Program_Format format = QDMI_PROGRAM_FORMAT_MAX;
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT,
                sizeof(QDMI_Program_Format), &format),
            QDMI_ERROR_INVALIDARGUMENT);

  for (const auto &program_format : fomac.get_supported_program_formats()) {
    ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                  job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT,
                  sizeof(QDMI_Program_Format), &program_format),
              QDMI_SUCCESS);
  }

  constexpr std::array unsupported_formats = {
      QDMI_PROGRAM_FORMAT_QASM2,
      QDMI_PROGRAM_FORMAT_QASM3,
      QDMI_PROGRAM_FORMAT_QIRBASEMODULE,
      QDMI_PROGRAM_FORMAT_QIRADAPTIVESTRING,
      QDMI_PROGRAM_FORMAT_QIRADAPTIVEMODULE,
      QDMI_PROGRAM_FORMAT_QPY,
      QDMI_PROGRAM_FORMAT_CUSTOM1,
      QDMI_PROGRAM_FORMAT_CUSTOM2,
      QDMI_PROGRAM_FORMAT_CUSTOM3,
      QDMI_PROGRAM_FORMAT_CUSTOM4,
      QDMI_PROGRAM_FORMAT_CUSTOM5};

  for (const auto &unsupported_format : unsupported_formats) {
    EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                  job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT,
                  sizeof(QDMI_Program_Format), &unsupported_format),
              QDMI_ERROR_NOTSUPPORTED);
  }

  // The MAX parameter is not a valid value for any device
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_MAX, 0, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  size_t shots = 5;
  EXPECT_EQ(
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM,
                                        sizeof(size_t), &shots),
      QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, 0, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_job_submit(nullptr), QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_device_job_check(job, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
  QDMI_Job_Status status{};
  EXPECT_EQ(IQM_QDMI_device_job_check(nullptr, &status),
            QDMI_ERROR_INVALIDARGUMENT);
  constexpr size_t timeout = 60;
  EXPECT_EQ(IQM_QDMI_device_job_wait(nullptr, timeout),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_device_job_cancel(job), QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_device_job_cancel(nullptr), QDMI_ERROR_INVALIDARGUMENT);
  IQM_QDMI_device_job_free(job);
}

TEST_F(QDMIIntegrationTest, OptionalJobParameters) {
  constexpr size_t shots_num = 64;
  const std::string heralding_mode = "none";
  const std::string move_validation_mode = "allow_prx";
  const std::string move_gate_frame_tracking_mode = "no_detuning_correction";
  const std::string dd_mode = "enabled";
  const auto qubit_sites = get_qubit_sites();
  ASSERT_GE(qubit_sites.size(), 2U);
  const auto first_qubit_name = fomac.get_site_name(qubit_sites[0]);
  const auto second_qubit_name = fomac.get_site_name(qubit_sites[1]);
  const auto qubit_mapping = std::map<std::string, std::string>{
      {"alice", first_qubit_name}, {"bob", second_qubit_name}};
  constexpr double max_circuit_duration_over_t2 = 0;
  constexpr size_t num_active_reset_cycles = 1;
  const std::string dd_strategy = R"({
    "merge_contiguous_waits": true,
    "target_qubits": [")" + first_qubit_name +
                                  "\",\n\"" + second_qubit_name + "\"\n]," +
                                  R"("skip_leading_wait": true,
    "skip_trailing_wait": true,
    "gate_sequences": [
      [9, "XYXYYXYX", "asap"],
      [5, "YXYX", "asap"],
      [2, "XX", "center"]
    ]
  })";

  // Transform the test program and replace the qubit names
  std::ostringstream mapping_program;
  mapping_program << R"({"name":"test_circuit","instructions":[)";
  mapping_program << R"({"name":"prx","locus":[")" << first_qubit_name
                  << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
  mapping_program << R"({"name":"prx","locus":[")" << second_qubit_name
                  << R"("],"args":{"angle_t":0.25,"phase_t":0.75}},)";
  mapping_program << R"({"name":"measure","locus":[")" << first_qubit_name
                  << R"("],"args":{"key":"meas_2_0_0"}},)";
  mapping_program << R"({"name":"measure","locus":[")" << second_qubit_name
                  << R"("],"args":{"key":"meas_2_0_1"}})";
  mapping_program << R"(],"metadata":{}})";
  std::string program = mapping_program.str();
  // Unified API supports qubit mapping
  for (const auto &[key, value] : qubit_mapping) {
    size_t pos = 0;
    while ((pos = program.find(value, pos)) != std::string::npos) {
      program.replace(pos, value.length(), key);
      pos += key.length();
    }
  }
  auto *job = fomac.submit_job(program, QDMI_PROGRAM_FORMAT_IQMJSON, shots_num,
                               heralding_mode, move_validation_mode,
                               move_gate_frame_tracking_mode, dd_mode,
                               qubit_mapping, max_circuit_duration_over_t2,
                               num_active_reset_cycles, dd_strategy);
  const auto status = wait_for_done(job);
  if (should_skip_for_expected_mock_failure(status)) {
    IQM_QDMI_device_job_free(job);
    GTEST_SKIP()
        << "Skipping because sirius:mock currently fails job execution in "
           "backend (known mock limitation), observed status "
        << status;
  }
  ASSERT_EQ(status, QDMI_JOB_STATUS_DONE);

  const auto counts = FoMaC::get_histogram(job);
  IQM_QDMI_device_job_free(job);
  size_t sum = 0;
  std::cout << "Counts: {\n";
  for (const auto &[key, count] : counts) {
    sum += count;
    std::cout << key << ": " << count << ",\n";
  }
  std::cout << "}\n";
  ASSERT_LE(sum, shots_num);
}

// Resonance currently rejects valid QIR programs with an internal server error.
// Track re-enabling this test in
// https://github.com/iqm-finland/QDMI-on-IQM/issues/170.
TEST_F(QDMIIntegrationTest, DISABLED_JobCycleQIR) {
  constexpr size_t shots_num = 64;
  const auto qir_program = build_qir_test_circuit();
  auto *job = fomac.submit_job(qir_program, QDMI_PROGRAM_FORMAT_QIRBASESTRING,
                               shots_num);
  const auto status = wait_for_done(job);
  if (should_skip_for_expected_mock_failure(status)) {
    IQM_QDMI_device_job_free(job);
    GTEST_SKIP()
        << "Skipping because sirius:mock currently fails job execution in "
           "backend (known mock limitation), observed status "
        << status;
  }
  ASSERT_EQ(status, QDMI_JOB_STATUS_DONE);

  const auto counts = FoMaC::get_histogram(job);

  size_t sum = 0;
  std::cout << "Counts: {\n";
  for (const auto &[key, count] : counts) {
    sum += count;
    std::cout << key << ": " << count << ",\n";
  }
  std::cout << "}\n";
  ASSERT_EQ(sum, shots_num);
}

TEST_F(QDMIIntegrationTest, FailedJobErrorLog) {
  testing::internal::CaptureStderr();
  IQM_QDMI_Device_Job job{};
  auto ret = IQM_QDMI_device_session_create_device_job(session, &job);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  constexpr auto format = QDMI_PROGRAM_FORMAT_IQMJSON;
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(QDMI_Program_Format),
      &format);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  const auto failed_job_program = build_iqm_json_test_circuit();
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, failed_job_program.size() + 1,
      failed_job_program.c_str());
  ASSERT_EQ(ret, QDMI_SUCCESS);
  constexpr auto num_shots_invalid = static_cast<size_t>(0);
  ret =
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM,
                                        sizeof(size_t), &num_shots_invalid);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  ret = IQM_QDMI_device_job_submit(job);
  ASSERT_EQ(ret, QDMI_ERROR_FATAL);
  const std::string output = testing::internal::GetCapturedStderr();
  EXPECT_NE(output.find("Job payload is not valid"), std::string::npos)
      << "Expected error message was not found in stderr output";

  testing::internal::CaptureStderr();
  auto status = QDMI_JOB_STATUS_CREATED;
  ret = IQM_QDMI_device_job_check(job, &status);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  ASSERT_EQ(status, QDMI_JOB_STATUS_FAILED);

  const auto captured_output = testing::internal::GetCapturedStderr();
  std::cout << captured_output << '\n';

  IQM_QDMI_device_job_free(job);
}

TEST_F(QDMIIntegrationTest, CalibrationJob) {
  // Calibration endpoints are optional in unified API
  // Legacy cocos endpoints may not be available on all servers
  IQM_QDMI_Device_Job job{};
  ASSERT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
            QDMI_SUCCESS);

  // Set job parameters
  constexpr QDMI_Program_Format format = QDMI_PROGRAM_FORMAT_CALIBRATION;
  const auto ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(QDMI_Program_Format),
      &format);
  if (ret == QDMI_ERROR_NOTSUPPORTED) {
    GTEST_SKIP() << "Calibration endpoint is not supported";
  }

  GTEST_SKIP() << "Skipping calibration job per default to avoid messing with"
                  " device calibration state during regular tests.";
  ASSERT_EQ(ret, QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CALIBRATION_CONFIG) + 1, TEST_CALIBRATION_CONFIG),
            QDMI_SUCCESS);

  // Try to submit the calibration job
  const auto submit_result = IQM_QDMI_device_job_submit(job);
  ASSERT_EQ(submit_result, QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_job_wait(job, DEFAULT_JOB_WAIT_TIMEOUT),
            QDMI_SUCCESS);
  const auto calibration_set_id = FoMaC::get_calibration_set_id(job);
  EXPECT_FALSE(calibration_set_id.empty())
      << "Calibration job must return a valid calibration set ID";

  IQM_QDMI_device_job_free(job);
}
