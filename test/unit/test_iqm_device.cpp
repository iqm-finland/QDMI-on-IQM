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

#include "http_client.hpp"
#include "http_stub.hpp"
#include "iqm_auth.hpp"
#include "iqm_qdmi/device.h"
#include "logging.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cpr/response.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <gtest/gtest.h>
#include <iostream>
#include <new>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int Set_env_var_raw(const char *key, const char *value) {
#ifdef _WIN32
  return _putenv_s(key, value);
#else
  // NOLINTNEXTLINE(misc-include-cleaner)
  return setenv(key, value, 1);
#endif
}

int Unset_env_var_raw(const char *key) {
#ifdef _WIN32
  return _putenv_s(key, "");
#else
  // NOLINTNEXTLINE(misc-include-cleaner)
  return unsetenv(key);
#endif
}

class ScopedEnvVar {
public:
  explicit ScopedEnvVar(std::string name) : name_(std::move(name)) {
    if (const char *original_value = std::getenv(name_.c_str());
        original_value != nullptr) {
      original_value_ = original_value;
      had_original_value_ = true;
    }
  }

  ~ScopedEnvVar() {
    if (had_original_value_) {
      Set_env_var_raw(name_.c_str(), original_value_.c_str());
    } else {
      Unset_env_var_raw(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;
  ScopedEnvVar(ScopedEnvVar &&) = delete;
  ScopedEnvVar &operator=(ScopedEnvVar &&) = delete;

private:
  std::string name_;
  std::string original_value_;
  bool had_original_value_ = false;
};

class ScopedUnsetEnvVar {
public:
  explicit ScopedUnsetEnvVar(std::string name) : name_(std::move(name)) {
    if (const char *original_value = std::getenv(name_.c_str());
        original_value != nullptr) {
      original_value_ = original_value;
      had_original_value_ = true;
    }
    Unset_env_var_raw(name_.c_str());
  }

  ~ScopedUnsetEnvVar() {
    if (had_original_value_) {
      Set_env_var_raw(name_.c_str(), original_value_.c_str());
    } else {
      Unset_env_var_raw(name_.c_str());
    }
  }

  ScopedUnsetEnvVar(const ScopedUnsetEnvVar &) = delete;
  ScopedUnsetEnvVar &operator=(const ScopedUnsetEnvVar &) = delete;
  ScopedUnsetEnvVar(ScopedUnsetEnvVar &&) = delete;
  ScopedUnsetEnvVar &operator=(ScopedUnsetEnvVar &&) = delete;

private:
  std::string name_;
  std::string original_value_;
  bool had_original_value_ = false;
};

// ============================================================================
// TEST FIXTURES
// ============================================================================

class DeviceTest : public testing::Test {
protected:
  IQM_QDMI_Device_Session session = nullptr;

private:
  ScopedUnsetEnvVar base_url_env_{"IQM_BASE_URL"};
  ScopedUnsetEnvVar token_env_{"IQM_TOKEN"};
  ScopedUnsetEnvVar tokens_file_env_{"IQM_TOKENS_FILE"};
  ScopedUnsetEnvVar qc_id_env_{"IQM_QC_ID"};
  ScopedUnsetEnvVar qc_alias_env_{"IQM_QC_ALIAS"};

protected:
  void SetUp() override {
    iqm::Logger::get_instance().set_level(iqm::LOG_LEVEL::DEBUG);
    EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
    EXPECT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);
  }

  void TearDown() override {
    if (session != nullptr) {
      IQM_QDMI_device_session_free(session);
    }
    EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
  }
};

class DeviceIntegrationMockTest : public testing::Test {
protected:
  IQM_QDMI_Device_Session session = nullptr;
  iqm::test_support::HttpStub http_stub;

private:
  ScopedUnsetEnvVar base_url_env_{"IQM_BASE_URL"};
  ScopedUnsetEnvVar token_env_{"IQM_TOKEN"};
  ScopedUnsetEnvVar tokens_file_env_{"IQM_TOKENS_FILE"};
  ScopedUnsetEnvVar qc_id_env_{"IQM_QC_ID"};
  ScopedUnsetEnvVar qc_alias_env_{"IQM_QC_ALIAS"};

protected:
  const std::string list_quantum_computers_response = R"({
      "quantum_computers": [
        {
          "id": "01966208-f3ec-73b7-890d-100000000000",
          "station_control_version": "47.3.1",
          "alias": "default",
          "display_name": "My quantum computer"
        }
      ]
    })";
  const std::string get_static_quantum_architectures_response = R"([
      {
        "computational_resonators": [],
        "connectivity": [["QB1","QB2"]],
        "dut_label":"M160_W0_H01_Z99",
        "qubits":["QB1","QB2"]
      }
    ])";
  const std::string get_dynamic_quantum_architectures_response = R"(
      {
        "calibration_set_id": "f0fb4be5-e913-4a04-8c94-18d1bd842def",
        "qubits": [
          "QB1",
          "QB2"
        ],
        "computational_resonators": [],
        "gates": {
          "cz": {
            "implementations": {
              "tgss": {
                "loci": [
                  [
                    "QB1",
                    "QB2"
                  ]
                ]
              }
            },
            "default_implementation": "tgss",
            "override_default_implementation": {}
          },
          "measure": {
            "implementations": {
              "constant": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "constant",
            "override_default_implementation": {}
          },
          "measure_fidelity": {
            "implementations": {
              "constant": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "constant",
            "override_default_implementation": {}
          },
          "prx": {
            "implementations": {
              "drag_gaussian": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "drag_gaussian",
            "override_default_implementation": {}
          },
          "prx_12": {
            "implementations": {
              "modulated_drag_crf": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "modulated_drag_crf",
            "override_default_implementation": {}
          },
          "cc_prx": {
            "implementations": {
              "prx_composite": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "prx_composite",
            "override_default_implementation": {}
          },
          "reset_wait": {
            "implementations": {
              "reset_wait": {
                "loci": [
                  ["QB1"],
                  ["QB2"]
                ]
              }
            },
            "default_implementation": "reset_wait",
            "override_default_implementation": {}
          }
        }
      }
    )";
  const std::string get_calibration_set_quality_metrics_response = R"(
      {
        "calibration_set": null,
        "created_timestamp": "2025-12-09T11:50:44.461266Z",
        "describes_id": "f0fb4be5-e913-4a04-8c94-18d1bd842def",
        "dut_label": "M160_W0_H01_Z99",
        "end_timestamp": "2025-12-09T11:50:44.542799Z",
        "invalid": false,
        "observation_ids": null,
        "observation_set_id": "958461bd-77af-4510-881f-6d1ce639852f",
        "observation_set_type": "quality-metric-set",
        "observations": [
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB1.fidelity",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59883,
            "uncertainty": null,
            "unit": "",
            "value": 0.96
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB1.error_0_to_1",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59884,
            "uncertainty": null,
            "unit": "",
            "value": 0.02
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB1.error_1_to_0",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59885,
            "uncertainty": null,
            "unit": "",
            "value": 0.02
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB2.fidelity",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59886,
            "uncertainty": null,
            "unit": "",
            "value": 0.96
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB2.error_0_to_1",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59887,
            "uncertainty": null,
            "unit": "",
            "value": 0.02
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.802455",
            "dut_field": "metrics.ssro.measure.constant.QB2.error_1_to_0",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.802455",
            "observation_id": 59888,
            "uncertainty": null,
            "unit": "",
            "value": 0.02
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.808725",
            "dut_field": "characterization.model.QB1.t1_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.808725",
            "observation_id": 60436,
            "uncertainty": null,
            "unit": "s",
            "value": 0.00002
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.808725",
            "dut_field": "characterization.model.QB1.t2_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.808725",
            "observation_id": 60438,
            "uncertainty": null,
            "unit": "s",
            "value": 0.00001
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.808725",
            "dut_field": "characterization.model.QB1.t2_echo_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.808725",
            "observation_id": 60439,
            "uncertainty": null,
            "unit": "s",
            "value": 0.000015
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.808725",
            "dut_field": "characterization.model.QB2.t1_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.808725",
            "observation_id": 60440,
            "uncertainty": null,
            "unit": "s",
            "value": 0.00002
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.809182",
            "dut_field": "characterization.model.QB2.t2_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.809182",
            "observation_id": 59994,
            "uncertainty": null,
            "unit": "s",
            "value": 0.00001
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.809182",
            "dut_field": "characterization.model.QB2.t2_echo_time",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.809182",
            "observation_id": 60002,
            "uncertainty": null,
            "unit": "s",
            "value": 0.000015
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.809338",
            "dut_field": "metrics.rb.prx.drag_gaussian.QB1.fidelity:par=d2",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.809338",
            "observation_id": 60293,
            "uncertainty": null,
            "unit": "",
            "value": 0.99
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.809338",
            "dut_field": "metrics.rb.prx.drag_gaussian.QB2.fidelity:par=d2",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.809338",
            "observation_id": 60303,
            "uncertainty": null,
            "unit": "",
            "value": 0.99
          },
          {
            "created_timestamp": "2025-12-09T11:50:43.804245",
            "dut_field": "metrics.irb.cz.tgss.QB1__QB2.fidelity:par=d2",
            "invalid": false,
            "modified_timestamp": "2025-12-09T11:50:43.804245",
            "observation_id": 60061,
            "uncertainty": null,
            "unit": "",
            "value": 0.97
          }
        ]
      }
    )";
  const std::string cocos_health_response = R"({
      "services": [
        {
          "name": "CoCoS",
          "status": "alive"
        },
        {
          "name": "Station Control",
          "status": "alive"
        }
      ],
      "warnings": []
    })";

  void SetUp() override {
    EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
    EXPECT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);
    const std::string base_url = "https://localhost";
    EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                  session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL,
                  base_url.size() + 1, base_url.c_str()),
              QDMI_SUCCESS);
  }

  void TearDown() override {
    if (session != nullptr) {
      IQM_QDMI_device_session_free(session);
    }
    EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
  }

  /**
   * @brief Queue the sequence of GET responses that a successful session
   * initialization requires: the quantum-computer listing, the static and
   * dynamic quantum architectures, the calibration set quality metrics, and
   * the optional CoCoS health probe.
   */
  void queue_successful_initialization() {
    http_stub.queue_get(200, list_quantum_computers_response);
    http_stub.queue_get(200, get_static_quantum_architectures_response);
    http_stub.queue_get(200, get_dynamic_quantum_architectures_response);
    http_stub.queue_get(200, get_calibration_set_quality_metrics_response);
    http_stub.queue_get(200, cocos_health_response);
  }

  static constexpr auto TEST_CIRCUIT_IQM_JSON = R"(
    {
      "name": "test_circuit",
      "instructions": [
        {
          "name": "prx",
          "locus": [
            "QB1"
          ],
          "args": {
            "angle_t": 0.25,
            "phase_t": 0.75
          }
        },
        {
          "name": "cz",
          "locus": [
            "QB1",
            "QB2"
          ],
          "args": {}
        },
        {
          "name": "measure",
          "locus": [
            "QB1"
          ],
          "args": {
            "key": "meas_2_0_0"
          }
        }
      ],
      "metadata": {}
    }
  )";
};

class DeviceJobMockTest : public DeviceIntegrationMockTest {
protected:
  IQM_QDMI_Device_Job job = nullptr;

  void SetUp() override {
    DeviceIntegrationMockTest::SetUp();

    queue_successful_initialization();

    ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

    ASSERT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
              QDMI_SUCCESS);
  }

  void TearDown() override {
    if (job != nullptr) {
      IQM_QDMI_device_job_free(job);
    }
    DeviceIntegrationMockTest::TearDown();
  }
};

class DeviceIntegrationEnvMockTest : public DeviceIntegrationMockTest {
protected:
  void SetUp() override {
    EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
    EXPECT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);
  }
};

// ============================================================================
// UNIT TESTS - Basic functionality without external dependencies
// ============================================================================

TEST_F(DeviceIntegrationEnvMockTest,
       SessionInitializationUsesBaseUrlFromEnvironment) {
  const ScopedEnvVar base_url_env("IQM_BASE_URL");
  ASSERT_EQ(Set_env_var_raw("IQM_BASE_URL", "https://environment.example"), 0);
  ASSERT_STREQ(std::getenv("IQM_BASE_URL"), "https://environment.example");

  queue_successful_initialization();

  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
  ASSERT_FALSE(http_stub.get_urls().empty());
  EXPECT_EQ(http_stub.get_urls().front(),
            "https://environment.example/api/v1/quantum-computers");
}

TEST_F(DeviceIntegrationMockTest,
       SessionInitializationPrefersExplicitBaseUrlOverEnvironment) {
  const ScopedEnvVar base_url_env("IQM_BASE_URL");
  ASSERT_EQ(Set_env_var_raw("IQM_BASE_URL", "https://environment.example"), 0);

  queue_successful_initialization();

  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
  ASSERT_FALSE(http_stub.get_urls().empty());
  EXPECT_EQ(http_stub.get_urls().front(),
            "https://localhost/api/v1/quantum-computers");
}

TEST_F(DeviceIntegrationMockTest,
       DeviceSessionReusesConnectionPoolAfterInitialization) {
  queue_successful_initialization();

  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
  http_stub.queue_get(200, R"({"queue_length": 0})");
  size_t queue_length = 0;
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(queue_length, 0U);

  const auto &connection_pools = http_stub.get_connection_pools();
  ASSERT_EQ(connection_pools.size(), 6U);
  EXPECT_TRUE(std::ranges::all_of(connection_pools, [&](const auto *pool) {
    return pool == connection_pools.front();
  }));
}

TEST_F(DeviceIntegrationMockTest, QueryQueueLength) {
  queue_successful_initialization();
  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

  http_stub.queue_get(200, R"({"queue_length": 7})");
  size_t queue_length = 0;
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(queue_length, 7U);
  ASSERT_FALSE(http_stub.get_urls().empty());
  EXPECT_EQ(http_stub.get_urls().back(),
            "https://localhost/api/v1/quantum-computers/"
            "01966208-f3ec-73b7-890d-100000000000/queue-availability");
}

TEST_F(DeviceIntegrationMockTest, QueueLengthIsOptional) {
  queue_successful_initialization();
  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

  size_t queue_length = 0;
  http_stub.queue_get(404);
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  http_stub.queue_get(200, R"({"queue_length": -1})");
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  http_stub.queue_get(200, R"({"queue_length": "unknown"})");
  EXPECT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUEUELENGTH, sizeof(queue_length),
                &queue_length, nullptr),
            QDMI_ERROR_NOTSUPPORTED);
}

TEST_F(DeviceIntegrationMockTest, QubitCountExcludesComputationalResonators) {
  // A Star-topology device: three qubits around one computational resonator.
  http_stub.queue_get(200, list_quantum_computers_response);
  http_stub.queue_get(200, R"([
      {
        "computational_resonators": ["CR1"],
        "connectivity": [["QB1","CR1"],["QB2","CR1"],["QB3","CR1"]],
        "dut_label":"M160_W0_H01_Z99",
        "qubits":["QB1","QB2","QB3"]
      }
    ])");
  http_stub.queue_get(200, R"({
      "calibration_set_id": "f0fb4be5-e913-4a04-8c94-18d1bd842def",
      "qubits": ["QB1", "QB2", "QB3"],
      "computational_resonators": ["CR1"],
      "gates": {
        "measure": {
          "implementations": {
            "constant": {"loci": [["QB1"], ["QB2"], ["QB3"]]}
          },
          "default_implementation": "constant",
          "override_default_implementation": {}
        }
      }
    })");
  http_stub.queue_get(200, R"({"observations": []})");
  http_stub.queue_get(200, cocos_health_response);

  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

  size_t num_qubits = 0;
  ASSERT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUBITSNUM, sizeof(num_qubits),
                &num_qubits, nullptr),
            QDMI_SUCCESS);
  // Three qubits, not the four sites. Before the fix this reported 4.
  EXPECT_EQ(num_qubits, 3U);

  // The site list still covers qubits and resonators alike, which is what
  // QDMI expects of it.
  size_t sites_size = 0;
  ASSERT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_SITES, 0, nullptr, &sites_size),
            QDMI_SUCCESS);
  EXPECT_EQ(sites_size / sizeof(IQM_QDMI_Site), 4U);
}

TEST_F(DeviceIntegrationMockTest, QubitCountMatchesSiteCountWithoutResonators) {
  queue_successful_initialization();
  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

  size_t num_qubits = 0;
  ASSERT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_QUBITSNUM, sizeof(num_qubits),
                &num_qubits, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(num_qubits, 2U);

  size_t sites_size = 0;
  ASSERT_EQ(IQM_QDMI_device_session_query_device_property(
                session, QDMI_DEVICE_PROPERTY_SITES, 0, nullptr, &sites_size),
            QDMI_SUCCESS);
  EXPECT_EQ(sites_size / sizeof(IQM_QDMI_Site), 2U);
}

TEST_F(DeviceTest, SessionAllocation) {
  // Session should be allocated in SetUp
  EXPECT_NE(session, nullptr);

  // Test null pointer handling
  EXPECT_EQ(IQM_QDMI_device_session_alloc(nullptr), QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceTest, SessionParameterValidation) {
  // Test setting valid parameters
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, 6, "value"),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_TOKEN, 6, "value"),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE, 6, "value"),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_AUTHURL, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_USERNAME, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_PASSWORD, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM1, 6, "value"),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM2, 6, "value"),
            QDMI_SUCCESS);
  const uint64_t timeout_milliseconds = 1'234;
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                sizeof(timeout_milliseconds), &timeout_milliseconds),
            QDMI_SUCCESS);
  const uint64_t maximum_timeout =
      static_cast<uint64_t>(std::chrono::milliseconds::max().count());
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                sizeof(maximum_timeout), &maximum_timeout),
            QDMI_SUCCESS);
}

TEST_F(DeviceTest, SessionParameterInvalidArguments) {
  // Test null session
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                nullptr, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, 6, "value"),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test zero size but non-null value
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, 0, "test"),
            QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceTest, SessionParameterUnsupportedParameters) {
  // Test unsupported custom parameters
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM4, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM5, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);

  // Test MAX parameter
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_MAX, 6, "value"),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test invalid parameter enum
  EXPECT_EQ(
      IQM_QDMI_device_session_set_parameter(
          // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
          session, static_cast<QDMI_Device_Session_Parameter>(999), 6, "value"),
      QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceTest, SessionParameterNullValueSupport) {
  // Test parameter support checking with null values
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_TOKEN, 0, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_AUTHFILE, 0, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3, 0, nullptr),
            QDMI_SUCCESS);
}

TEST_F(DeviceTest, SessionRequestTimeoutRejectsInvalidValues) {
  const uint64_t zero_timeout = 0;
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                sizeof(zero_timeout), &zero_timeout),
            QDMI_ERROR_INVALIDARGUMENT);
  const uint32_t wrong_size = 10;
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                sizeof(wrong_size), &wrong_size),
            QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceTest, SessionInitializationWithoutParameters) {
  // Test initialization without setting required parameters
  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_ERROR_FATAL);
}

TEST_F(DeviceTest, JobCreationWithoutInitialization) {
  IQM_QDMI_Device_Job job = nullptr;
  // Try to create job without initializing session
  EXPECT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
            QDMI_ERROR_BADSTATE);
}

TEST_F(DeviceJobMockTest, JobRetrievalValidatesArguments) {
  IQM_QDMI_Device_Job retrieved_job = job;
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                nullptr, "job-123", &retrieved_job),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(retrieved_job, job);
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(session, nullptr,
                                                              &retrieved_job),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(retrieved_job, job);
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(session, "",
                                                              &retrieved_job),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(retrieved_job, job);
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", nullptr),
            QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceTest, JobRetrievalRequiresInitializedSession) {
  IQM_QDMI_Device_Job retrieved_job = nullptr;
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_BADSTATE);
  EXPECT_EQ(retrieved_job, nullptr);
}

// ============================================================================
// INTEGRATION MOCK TESTS - Full lifecycle with mocked dependencies
// ============================================================================

TEST_F(DeviceJobMockTest, RetrieveExistingJobById) {
  const std::string job_status_response =
      R"({"id": "job-123", "status": "ready", "type": "circuit"})";
  http_stub.queue_get(200, job_status_response);

  IQM_QDMI_Device_Job retrieved_job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_SUCCESS);

  size_t id_size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_query_property(retrieved_job,
                                               QDMI_DEVICE_JOB_PROPERTY_ID, 0,
                                               nullptr, &id_size),
            QDMI_SUCCESS);
  std::string id(id_size, '\0');
  ASSERT_EQ(IQM_QDMI_device_job_query_property(retrieved_job,
                                               QDMI_DEVICE_JOB_PROPERTY_ID,
                                               id.size(), id.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_STREQ(id.c_str(), "job-123");

  QDMI_Program_Format program_format = QDMI_PROGRAM_FORMAT_IQMJSON;
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                retrieved_job, QDMI_DEVICE_JOB_PROPERTY_PROGRAMFORMAT,
                sizeof(program_format), &program_format, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                retrieved_job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, 0, nullptr),
            QDMI_ERROR_BADSTATE);
  EXPECT_EQ(IQM_QDMI_device_job_submit(retrieved_job), QDMI_ERROR_BADSTATE);

  QDMI_Job_Status status = QDMI_JOB_STATUS_CREATED;
  EXPECT_EQ(IQM_QDMI_device_job_check(retrieved_job, &status), QDMI_SUCCESS);
  EXPECT_EQ(status, QDMI_JOB_STATUS_DONE);
  IQM_QDMI_device_job_free(retrieved_job);
}

TEST_F(DeviceJobMockTest, RetrievedQueuedJobReportsFreshQueuePosition) {
  http_stub.queue_get(
      200,
      R"({"id":"job-123","status":"waiting","type":"circuit","queue_position":9})");

  IQM_QDMI_Device_Job retrieved_job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_SUCCESS);

  http_stub.queue_get(
      200,
      R"({"id":"job-123","status":"waiting","type":"circuit","queue_position":4})");
  size_t queue_position = 0;
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                retrieved_job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(queue_position, 4U);

  const auto &get_urls = http_stub.get_urls();
  ASSERT_GE(get_urls.size(), 2U);
  EXPECT_EQ(get_urls[get_urls.size() - 2],
            "https://localhost/api/v1/jobs/job-123");
  EXPECT_EQ(get_urls.back(), "https://localhost/api/v1/jobs/job-123");
  IQM_QDMI_device_job_free(retrieved_job);
}

TEST_F(DeviceJobMockTest, RetrieveExistingJobRestoresRemoteStatus) {
  constexpr std::array status_cases{
      std::pair{"received", QDMI_JOB_STATUS_SUBMITTED},
      std::pair{"queued", QDMI_JOB_STATUS_QUEUED},
      std::pair{"aborted", QDMI_JOB_STATUS_CANCELED},
      std::pair{"failed", QDMI_JOB_STATUS_FAILED},
  };

  for (const auto &[native_status, qdmi_status] : status_cases) {
    const auto response = std::string{R"({"id":"job-123","status":")"} +
                          native_status + R"(","type":"circuit"})";
    http_stub.queue_get(200, response);
    IQM_QDMI_Device_Job retrieved_job = nullptr;
    ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                  session, "job-123", &retrieved_job),
              QDMI_SUCCESS);

    if (qdmi_status == QDMI_JOB_STATUS_SUBMITTED ||
        qdmi_status == QDMI_JOB_STATUS_QUEUED) {
      http_stub.queue_get(200, response);
    }
    QDMI_Job_Status status = QDMI_JOB_STATUS_CREATED;
    ASSERT_EQ(IQM_QDMI_device_job_check(retrieved_job, &status), QDMI_SUCCESS);
    EXPECT_EQ(status, qdmi_status);

    if (qdmi_status == QDMI_JOB_STATUS_SUBMITTED) {
      http_stub.queue_get(
          200, R"({"id":"job-123","status":"unknown","type":"circuit"})");
      EXPECT_EQ(IQM_QDMI_device_job_check(retrieved_job, &status),
                QDMI_ERROR_FATAL);
    }
    IQM_QDMI_device_job_free(retrieved_job);
  }

  http_stub.queue_get(
      200, R"({"id":"job-123","status":"unknown","type":"circuit"})");
  IQM_QDMI_Device_Job retrieved_job = job;
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(retrieved_job, nullptr);
}

TEST_F(DeviceJobMockTest, RetrieveExistingJobPropagatesAccessErrors) {
  IQM_QDMI_Device_Job retrieved_job = nullptr;
  http_stub.queue_get(404, R"({"message": "Job not found"})");
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "unknown", &retrieved_job),
            QDMI_ERROR_NOTFOUND);

  http_stub.queue_get(403, R"({"message": "Access denied"})");
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "private", &retrieved_job),
            QDMI_ERROR_PERMISSIONDENIED);
}

TEST_F(DeviceJobMockTest, RetrieveExistingJobRejectsMalformedResponses) {
  IQM_QDMI_Device_Job retrieved_job = job;
  http_stub.queue_get(200, "invalid json");
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(retrieved_job, nullptr);

  retrieved_job = job;
  http_stub.queue_get(200, R"({"id": "job-123", "type": "circuit"})");
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(retrieved_job, nullptr);
}

TEST_F(DeviceJobMockTest, RetrieveExistingJobContainsBoundaryExceptions) {
  IQM_QDMI_Device_Job retrieved_job = job;
  auto &get_hook = iqm::http::internal::Get_hooks().get;

  get_hook = [](const auto &, const auto &, const auto &, const auto &,
                const auto) -> cpr::Response {
    throw iqm::ClientAuthenticationError{"expired token"};
  };
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_PERMISSIONDENIED);
  EXPECT_EQ(retrieved_job, nullptr);

  retrieved_job = job;
  get_hook = [](const auto &, const auto &, const auto &, const auto &,
                const auto) -> cpr::Response { throw std::bad_alloc{}; };
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_OUTOFMEM);
  EXPECT_EQ(retrieved_job, nullptr);

  retrieved_job = job;
  get_hook = [](const auto &, const auto &, const auto &, const auto &,
                const auto) -> cpr::Response {
    throw std::runtime_error{"transport hook failed"};
  };
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(retrieved_job, nullptr);

  retrieved_job = job;
  get_hook = [](const auto &, const auto &, const auto &, const auto &,
                const auto) -> cpr::Response { throw 42; };
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(retrieved_job, nullptr);
}

TEST_F(DeviceJobMockTest, RetrieveExistingJobRejectsUnsupportedJobTypes) {
  http_stub.queue_get(
      200,
      R"({"id": "calibration-123", "status": "ready", "type": "calibration"})");
  IQM_QDMI_Device_Job retrieved_job = nullptr;
  EXPECT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "calibration-123", &retrieved_job),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(retrieved_job, nullptr);
}

TEST_F(DeviceJobMockTest, RetrievedJobReturnsShotsWithoutSubmissionMetadata) {
  http_stub.queue_get(
      200, R"({"id": "job-123", "status": "ready", "type": "circuit"})");
  IQM_QDMI_Device_Job retrieved_job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_SUCCESS);

  http_stub.queue_get(
      200,
      R"([{"measurement_keys": ["meas_2_0_0", "meas_2_0_1"], "counts": {"01": 2, "10": 1}}])");
  http_stub.queue_get(
      200,
      R"([{"meas_2_0_0": [[0], [1], [0]], "meas_2_0_1": [[1], [0], [1]]}])");
  size_t shots_size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(
                retrieved_job, QDMI_JOB_RESULT_SHOTS, 0, nullptr, &shots_size),
            QDMI_SUCCESS);
  std::vector<char> shots(shots_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(retrieved_job,
                                            QDMI_JOB_RESULT_SHOTS, shots.size(),
                                            shots.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_STREQ(shots.data(), "01,10,01");
  IQM_QDMI_device_job_free(retrieved_job);
}

TEST_F(DeviceJobMockTest, RetrievedJobPreservesPermissionFailures) {
  http_stub.queue_get(
      200, R"({"id": "job-123", "status": "running", "type": "circuit"})");
  IQM_QDMI_Device_Job retrieved_job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_retrieve_device_job_by_id(
                session, "job-123", &retrieved_job),
            QDMI_SUCCESS);

  http_stub.queue_get(403, R"({"message": "Access denied"})");
  QDMI_Job_Status status = QDMI_JOB_STATUS_CREATED;
  EXPECT_EQ(IQM_QDMI_device_job_check(retrieved_job, &status),
            QDMI_ERROR_PERMISSIONDENIED);

  http_stub.queue_post(403, R"({"message": "Access denied"})");
  EXPECT_EQ(IQM_QDMI_device_job_cancel(retrieved_job),
            QDMI_ERROR_PERMISSIONDENIED);

  http_stub.queue_get(
      200, R"({"id": "job-123", "status": "running", "type": "circuit"})");
  EXPECT_EQ(IQM_QDMI_device_job_check(retrieved_job, &status), QDMI_SUCCESS);
  EXPECT_EQ(status, QDMI_JOB_STATUS_RUNNING);
  IQM_QDMI_device_job_free(retrieved_job);
}

TEST_F(DeviceJobMockTest, FullLifecycle) {
  const std::string job_submission_response = R"({"id": "job-123"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_results_response =
      R"([{"measurement_keys": ["meas_2_0_0"], "counts": {"0": 100, "1": 0}}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_results_response);

  // Job submission
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 100;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);

  // Wait for job completion
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  // Check results
  size_t hist_keys_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &hist_keys_size),
            QDMI_SUCCESS);
  std::string hist_keys(hist_keys_size, '\0');
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                            hist_keys_size, hist_keys.data(),
                                            nullptr),
            QDMI_SUCCESS);

  size_t hist_values_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES, 0,
                                            nullptr, &hist_values_size),
            QDMI_SUCCESS);
  std::vector<size_t> hist_values(hist_values_size / sizeof(size_t));
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                            hist_values_size,
                                            hist_values.data(), nullptr),
            QDMI_SUCCESS);
}

TEST_F(DeviceJobMockTest, HistogramKeysOfDifferingLength) {
  http_stub.queue_post(200, R"({"id": "job-123"})");
  http_stub.queue_get(200, R"({"status": "ready"})");
  // The keys are stored in a std::map, which orders the shorter one first, so
  // sizing the buffer from the first key under-counts what the writes need.
  http_stub.queue_get(
      200, R"([{"measurement_keys": ["m"], "counts": {"00": 5, "111": 3}}])");

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 8;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  size_t hist_keys_size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &hist_keys_size),
            QDMI_SUCCESS);
  // "00" + ',' + "111" + '\0'
  EXPECT_EQ(hist_keys_size, 7U);

  // Allocate exactly what the API asked for. Before the fix this reported 6 and
  // the write below overran the buffer by one byte.
  std::vector<char> hist_keys(hist_keys_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                            hist_keys_size, hist_keys.data(),
                                            nullptr),
            QDMI_SUCCESS);
  EXPECT_STREQ(hist_keys.data(), "00,111");
}

TEST_F(DeviceJobMockTest, HistogramKeysOfEqualLengthAreUnaffected) {
  http_stub.queue_post(200, R"({"id": "job-123"})");
  http_stub.queue_get(200, R"({"status": "ready"})");
  http_stub.queue_get(
      200,
      R"([{"measurement_keys": ["m"], "counts": {"00": 5, "01": 2, "11": 1}}])");

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 8;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  size_t hist_keys_size = 0;
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &hist_keys_size),
            QDMI_SUCCESS);
  // Unchanged from the previous computation for uniform keys: 3 * (2 + 1).
  EXPECT_EQ(hist_keys_size, 9U);

  std::vector<char> hist_keys(hist_keys_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                            hist_keys_size, hist_keys.data(),
                                            nullptr),
            QDMI_SUCCESS);
  EXPECT_STREQ(hist_keys.data(), "00,01,11");
}

TEST_F(DeviceJobMockTest, SubmissionUsesCanonicalRunRequestFields) {
  http_stub.queue_post(200, R"({"id": "job-canonical-fields"})");

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr auto move_validation = "allow_prx";
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM2,
                strlen(move_validation) + 1, move_validation),
            QDMI_SUCCESS);
  constexpr auto frame_tracking = "no_detuning_correction";
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_CUSTOM3,
                strlen(frame_tracking) + 1, frame_tracking),
            QDMI_SUCCESS);
  constexpr size_t active_reset_cycles = 3;
  // The IQM extension parameters continue past QDMI's last named custom value.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job,
                static_cast<QDMI_Device_Job_Parameter>(
                    QDMI_DEVICE_JOB_PARAMETER_CUSTOM5 + 2),
                sizeof(active_reset_cycles), &active_reset_cycles),
            QDMI_SUCCESS);

  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(http_stub.post_bodies().size(), 1U);
  // NOLINTNEXTLINE(misc-include-cleaner)
  const auto request = nlohmann::json::parse(http_stub.post_bodies().front());
  EXPECT_EQ(request.at("move_gate_validation"), move_validation);
  EXPECT_EQ(request.at("move_gate_frame_tracking"), frame_tracking);
  EXPECT_EQ(request.at("active_reset_cycles"), active_reset_cycles);
  EXPECT_FALSE(request.contains("move_validation_mode"));
  EXPECT_FALSE(request.contains("move_gate_frame_tracking_mode"));
  EXPECT_FALSE(request.contains("num_active_reset_cycles"));
}

TEST_F(DeviceJobMockTest, ReplacingProgramUsesLatestValueForSubmission) {
  constexpr auto replacement_program =
      R"({"name":"replacement","instructions":[],"metadata":{}})";
  http_stub.queue_post(200, R"({"id": "job-replaced-program"})");

  constexpr auto format = QDMI_PROGRAM_FORMAT_QIRBASESTRING;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(format),
                &format),
            QDMI_SUCCESS);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(replacement_program) + 1, replacement_program),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, 0, nullptr),
            QDMI_SUCCESS);

  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(http_stub.post_bodies().size(), 1U);
  const auto &request_body = http_stub.post_bodies().front();
  EXPECT_NE(request_body.find("replacement"), std::string::npos);
  EXPECT_EQ(request_body.find("test_circuit"), std::string::npos);
}

TEST_F(DeviceJobMockTest, RetrieveShotMeasurements) {
  const std::string job_submission_response = R"({"id": "job-456"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["meas_2_0_0", "meas_2_0_1"], "counts": {"00": 1, "01": 1, "10": 1, "11": 1}}])";
  const std::string job_measurements_response =
      R"([{"meas_2_0_0": [[0], [1], [0], [1]], "meas_2_0_1": [[0], [0], [1], [1]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  // Job submission
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 4;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);

  // Wait for job completion
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  // Check shot results
  size_t shots_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_SUCCESS);
  ASSERT_GT(shots_size, 0);

  std::vector<char> shots_buffer(shots_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                            shots_size, shots_buffer.data(),
                                            nullptr),
            QDMI_SUCCESS);

  // Verify the format: comma-separated bitstrings
  const std::string shots_data(shots_buffer.data());
  EXPECT_EQ(shots_data, "00,10,01,11");

  // Test buffer size validation - buffer too small should fail
  std::vector<char> small_buffer(shots_size - 1);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                            small_buffer.size(),
                                            small_buffer.data(), nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  // This should NOT trigger another API call (only 3 GET responses were
  // queued above; an unscripted call would fail the test).
  size_t hist_keys_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &hist_keys_size),
            QDMI_SUCCESS);
  ASSERT_GT(hist_keys_size, 0);

  std::vector<char> hist_keys_buffer(hist_keys_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS,
                                            hist_keys_size,
                                            hist_keys_buffer.data(), nullptr),
            QDMI_SUCCESS);

  // Verify histogram was computed from shots
  const std::string hist_keys_data(hist_keys_buffer.data());
  // Should have all 4 unique bitstrings
  EXPECT_TRUE(hist_keys_data.find("00") != std::string::npos);
  EXPECT_TRUE(hist_keys_data.find("01") != std::string::npos);
  EXPECT_TRUE(hist_keys_data.find("10") != std::string::npos);
  EXPECT_TRUE(hist_keys_data.find("11") != std::string::npos);

  // Get histogram values (counts)
  size_t hist_values_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES, 0,
                                            nullptr, &hist_values_size),
            QDMI_SUCCESS);
  std::vector<size_t> hist_values(hist_values_size / sizeof(size_t));
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_VALUES,
                                            hist_values_size,
                                            hist_values.data(), nullptr),
            QDMI_SUCCESS);

  // Each bitstring appears exactly once in our test data
  size_t total_count = 0;
  for (const auto count : hist_values) {
    EXPECT_EQ(count, 1);
    total_count += count;
  }
  EXPECT_EQ(total_count, 4); // 4 shots total
}

TEST_F(DeviceJobMockTest, ShotOrderMatchesIQMMeasurementCounts) {
  const std::string job_submission_response = R"({"id": "job-ordered"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["z_result", "a_result"], "counts": {"010": 1, "101": 1}}])";
  const std::string job_measurements_response =
      R"([{"a_result": [[0], [1]], "z_result": [[0, 1], [1, 0]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 2;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  size_t hist_keys_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &hist_keys_size),
            QDMI_SUCCESS);

  size_t shots_size{};
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_SUCCESS);
  std::vector<char> shots_buffer(shots_size);
  ASSERT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                            shots_size, shots_buffer.data(),
                                            nullptr),
            QDMI_SUCCESS);
  EXPECT_STREQ(shots_buffer.data(), "010,101");
}

TEST_F(DeviceJobMockTest, RetrieveShotsBeforeCompletion) {
  const std::string job_submission_response = R"({"id": "job-789"})";

  http_stub.queue_post(200, job_submission_response);

  // Job submission
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 4;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);

  // Attempt to retrieve shots before job completion should fail
  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceJobMockTest, RejectInvalidMeasurementFormat) {
  const std::string job_submission_response = R"({"id": "job-789"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["m"], "counts": {"0": 1}}])";
  const std::string job_measurements_response = R"([{"m": [[]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 1;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  // Should fail when trying to retrieve results due to invalid format
  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_ERROR_FATAL);
}

TEST_F(DeviceJobMockTest, HandleInvalidQueuePositionTypes) {
  // Test that invalid queue_position types don't cause exceptions
  const std::string job_submission_response_string_queue =
      R"({"id": "job-123", "queue_position": "invalid"})";

  // Test with string queue_position - should succeed but ignore queue position
  http_stub.queue_post(200, job_submission_response_string_queue);
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 100;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
}

TEST_F(DeviceJobMockTest, QueryQueuePositionRefreshesJobStatus) {
  http_stub.queue_post(200, R"({"id": "job-queue"})");
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);

  http_stub.queue_get(200, R"({"status": "waiting", "queue_position": 4})");
  size_t queue_position = 0;
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(queue_position, 4U);

  http_stub.queue_get(200, R"({"status": "waiting", "queue_position": 2})");
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(queue_position, 2U);

  const auto &get_urls = http_stub.get_urls();
  ASSERT_GE(get_urls.size(), 2U);
  EXPECT_EQ(get_urls[get_urls.size() - 2],
            "https://localhost/api/v1/jobs/job-queue");
  EXPECT_EQ(get_urls.back(), "https://localhost/api/v1/jobs/job-queue");
}

TEST_F(DeviceJobMockTest, QueuePositionRequiresQueuedJobAndKnownPosition) {
  size_t queue_position = 0;
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_ERROR_BADSTATE);

  http_stub.queue_post(200, R"({"id": "job-queue"})");
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);

  http_stub.queue_get(200, R"({"status": "waiting"})");
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  http_stub.queue_get(200, R"({"status": "waiting", "queue_position": -1})");
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_ERROR_NOTSUPPORTED);

  http_stub.queue_get(200, R"({"status": "running", "queue_position": 3})");
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_QUEUEPOSITION,
                sizeof(queue_position), &queue_position, nullptr),
            QDMI_ERROR_BADSTATE);
}

TEST_F(DeviceJobMockTest, RejectNonIntegerMeasurementValues) {
  const std::string job_submission_response = R"({"id": "job-999"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["meas_2_0_0"], "counts": {"00": 1}}])";
  // Invalid: the second value is a string instead of an integer.
  const std::string job_measurements_response =
      R"([{"meas_2_0_0": [[0, "1"]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 1;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  // Should fail when trying to retrieve results due to non-integer value
  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_ERROR_FATAL);
}

TEST_F(DeviceJobMockTest, RejectNonBitMeasurementValues) {
  const std::string job_submission_response = R"({"id": "job-999"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["meas_2_0_0"], "counts": {"00": 1}}])";
  const std::string job_measurements_response = R"([{"meas_2_0_0": [[0, 2]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 1;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_ERROR_FATAL);
}

TEST_F(DeviceJobMockTest, RejectInconsistentMeasurementWidths) {
  const std::string job_submission_response = R"({"id": "job-width"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": ["m"], "counts": {"0": 1, "01": 1}}])";
  const std::string job_measurements_response = R"([{"m": [[0], [0, 1]]}])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 2;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  std::array<char, 8> buffer{};
  buffer.fill('x');
  const auto original_buffer = buffer;
  size_t shots_size = 123;
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS,
                                            buffer.size(), buffer.data(),
                                            &shots_size),
            QDMI_ERROR_FATAL);
  EXPECT_EQ(shots_size, 123);
  EXPECT_EQ(buffer, original_buffer);
}

TEST_F(DeviceJobMockTest, EmptyShotsReturnsNullTerminator) {
  const std::string job_submission_response = R"({"id": "job-empty"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_counts_response =
      R"([{"measurement_keys": [], "counts": {}}])";
  // Empty measurements array - no shots
  const std::string job_measurements_response = R"([])";

  http_stub.queue_post(200, job_submission_response);
  http_stub.queue_get(200, job_status_response);
  http_stub.queue_get(200, job_counts_response);
  http_stub.queue_get(200, job_measurements_response);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 1;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_submit(job), QDMI_SUCCESS);
  ASSERT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  // Should return size 1 (for null terminator) even with no shots
  size_t shots_size{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 0,
                                            nullptr, &shots_size),
            QDMI_SUCCESS);
  EXPECT_EQ(shots_size, 1);

  // Should write just a null terminator
  std::vector<char> buffer(1);
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_SHOTS, 1,
                                            buffer.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(buffer[0], '\0');
  const std::string result(buffer.data());
  EXPECT_TRUE(result.empty());
}

constexpr auto TEST_CALIBRATION_CONFIG = R"(
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

TEST_F(DeviceJobMockTest, FullLifecycleCalibration) {
  // Job submission
  auto ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
      strlen(TEST_CALIBRATION_CONFIG) + 1, TEST_CALIBRATION_CONFIG);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  constexpr auto format = QDMI_PROGRAM_FORMAT_CALIBRATION;
  ret = IQM_QDMI_device_job_set_parameter(
      job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT, sizeof(format), &format);
  ASSERT_EQ(ret, QDMI_SUCCESS);

  const std::string job_submission_response = R"({"id": "job-123"})";
  http_stub.queue_post(200, job_submission_response);
  ret = IQM_QDMI_device_job_submit(job);
  ASSERT_EQ(ret, QDMI_SUCCESS);

  const std::string job_status_response = R"({"status": "ready"})";
  http_stub.queue_get(200, job_status_response);
  EXPECT_EQ(IQM_QDMI_device_job_wait(job, 0), QDMI_SUCCESS);

  const std::string job_results_response = R"({
    "status": "ready",
    "result": {
      "success": true,
      "calibration_set_id": "4286b859-30a7-4036-8c25-1e42ddd85c0e",
      "calibration_observations_tag": "qccsw_recal_2025-12-09T23:58:19.269193_cal",
      "calibration_error": null,
      "base_run_id": null
    },
    "start_time": "2025-12-09T23:57:54.199660",
    "end_time": "2025-12-09T23:58:24.465053",
    "run_config": {}
  })";
  http_stub.queue_get(200, job_results_response);
  http_stub.queue_get(200, get_dynamic_quantum_architectures_response);
  http_stub.queue_get(200, get_calibration_set_quality_metrics_response);

  size_t size = 0;
  ret = IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM1, 0,
                                        nullptr, &size);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  std::string calibration_set_id(size - 1, '\0');
  ret = IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_CUSTOM1, size,
                                        calibration_set_id.data(), nullptr);
  ASSERT_EQ(ret, QDMI_SUCCESS);
  EXPECT_FALSE(calibration_set_id.empty())
      << "Calibration job must return a valid calibration set ID";
}

// ============================================================================
// ERROR HANDLING AND EDGE CASE TESTS
// ============================================================================

TEST_F(DeviceIntegrationMockTest, HTTPTimeoutHandling) {

  // Test network timeout (HTTP 408 Request Timeout)
  http_stub.queue_get(408);

  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_ERROR_TIMEOUT);
}

TEST_F(DeviceIntegrationMockTest,
       SessionRequestTimeoutAppliesToInitialization) {
  const uint64_t timeout_milliseconds = 1'234;
  ASSERT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3,
                sizeof(timeout_milliseconds), &timeout_milliseconds),
            QDMI_SUCCESS);
  queue_successful_initialization();

  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
  ASSERT_EQ(http_stub.get_timeouts().size(), 5U);
  EXPECT_TRUE(
      std::ranges::all_of(http_stub.get_timeouts(), [](const auto timeout) {
        return timeout == std::chrono::milliseconds{1'234};
      }));
}

TEST_F(DeviceIntegrationMockTest, HTTPAuthenticationFailure) {

  // Test authentication failure (HTTP 401 Unauthorized)
  http_stub.queue_get(401);

  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_ERROR_PERMISSIONDENIED);
}

TEST_F(DeviceIntegrationMockTest,
       MissingCocosHealthEndpointDisablesCalibrationJobs) {
  struct Logger_guard {
    iqm::Logger *logger;
    iqm::LOG_LEVEL original_level;

    Logger_guard(iqm::Logger *logger_in, const iqm::LOG_LEVEL level)
        : logger(logger_in), original_level(level) {}

    Logger_guard(const Logger_guard &) = delete;
    Logger_guard &operator=(const Logger_guard &) = delete;
    Logger_guard(Logger_guard &&) = delete;
    Logger_guard &operator=(Logger_guard &&) = delete;

    ~Logger_guard() {
      logger->set_output(std::cerr);
      logger->set_level(original_level);
    }
  };

  std::stringstream log_stream{};
  auto &logger = iqm::Logger::get_instance();
  const Logger_guard logger_guard{&logger, logger.get_level()};
  logger.set_level(iqm::LOG_LEVEL::DEBUG);
  logger.set_output(log_stream);

  http_stub.queue_get(200, list_quantum_computers_response);
  http_stub.queue_get(200, get_static_quantum_architectures_response);
  http_stub.queue_get(200, get_dynamic_quantum_architectures_response);
  http_stub.queue_get(200, get_calibration_set_quality_metrics_response);
  // Optional CoCoS health probe: 404 means calibration jobs are unsupported.
  http_stub.queue_get(404);

  ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);

  IQM_QDMI_Device_Job job = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
            QDMI_SUCCESS);

  constexpr auto format = QDMI_PROGRAM_FORMAT_CALIBRATION;
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAMFORMAT,
                sizeof(QDMI_Program_Format), &format),
            QDMI_ERROR_NOTSUPPORTED);

  const auto logs = log_stream.str();
  EXPECT_EQ(logs.find("ERROR"), std::string::npos);

  IQM_QDMI_device_job_free(job);
}

TEST_F(DeviceIntegrationMockTest, MalformedJSONResponse) {
  http_stub.queue_get(200, "invalid json");

  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_ERROR_FATAL);

  // Failed initialization leaves the session configurable and retryable.
  const std::string base_url = "https://retry.example.com";
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL,
                base_url.size() + 1, base_url.c_str()),
            QDMI_SUCCESS);
  queue_successful_initialization();
  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
}

TEST_F(DeviceIntegrationMockTest, SessionInitializationContainsCxxExceptions) {
  auto &get_hook = iqm::http::internal::Get_hooks().get;
  auto original_get_hook = get_hook;
  const auto expect_mapped_exception = [&](const std::exception_ptr &exception,
                                           const int expected_status) {
    get_hook = [exception](const auto &, const auto &, const auto &,
                           const auto &, const auto) -> cpr::Response {
      std::rethrow_exception(exception);
    };
    EXPECT_EQ(IQM_QDMI_device_session_init(session), expected_status);
  };

  expect_mapped_exception(
      std::make_exception_ptr(iqm::ClientAuthenticationError{"denied"}),
      QDMI_ERROR_PERMISSIONDENIED);
  expect_mapped_exception(std::make_exception_ptr(std::bad_alloc{}),
                          QDMI_ERROR_OUTOFMEM);
  expect_mapped_exception(
      std::make_exception_ptr(std::runtime_error{"transport failure"}),
      QDMI_ERROR_FATAL);
  expect_mapped_exception(std::make_exception_ptr(42), QDMI_ERROR_FATAL);

  get_hook = std::move(original_get_hook);
  queue_successful_initialization();
  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
}

TEST_F(DeviceJobMockTest, DoubleInitializationPrevention) {
  // The session was already initialized in SetUp(); re-initializing must be
  // rejected before any HTTP request is made.
  EXPECT_EQ(IQM_QDMI_device_session_init(session), QDMI_ERROR_BADSTATE);
}

TEST_F(DeviceJobMockTest, JobSubmissionFailure) {
  // Mock job submission failure (HTTP 500 Internal Server Error)
  http_stub.queue_post(500);

  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(TEST_CIRCUIT_IQM_JSON) + 1, TEST_CIRCUIT_IQM_JSON),
            QDMI_SUCCESS);
  constexpr size_t shots = 100;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, sizeof(shots), &shots),
            QDMI_SUCCESS);

  // Job submission should fail
  EXPECT_EQ(IQM_QDMI_device_job_submit(job), QDMI_ERROR_FATAL);
}

// ============================================================================
// JOB HANDLING TESTS
// ============================================================================

} // namespace

TEST_F(DeviceJobMockTest, JobParameterValidation) {
  // Test null job parameter
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                nullptr, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, 1, "test"),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test invalid parameter enum
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                job, static_cast<QDMI_Device_Job_Parameter>(999), 1, "test"),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test parameter support checking with null values
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, 0, nullptr),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM, 0, nullptr),
            QDMI_SUCCESS);
}

TEST_F(DeviceJobMockTest, ProgramPropertyReturnsLatestCopiedBytes) {
  size_t size = 0;
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, 0, nullptr, &size),
            QDMI_ERROR_BADSTATE);

  constexpr auto first_program_expected = std::to_array("first");
  auto first_program = first_program_expected;
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, first_program.size(),
                first_program.data()),
            QDMI_SUCCESS);
  first_program.front() = 'X';

  ASSERT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, 0, nullptr, &size),
            QDMI_SUCCESS);
  ASSERT_EQ(size, first_program_expected.size());
  std::vector<char> retrieved_program(size);
  ASSERT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, retrieved_program.size(),
                retrieved_program.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_TRUE(std::ranges::equal(retrieved_program, first_program_expected));

  constexpr auto latest_program = std::to_array("latest program");
  ASSERT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM, latest_program.size(),
                latest_program.data()),
            QDMI_SUCCESS);

  ASSERT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, 0, nullptr, &size),
            QDMI_SUCCESS);
  ASSERT_EQ(size, latest_program.size());

  std::vector<char> too_small(size - 1);
  EXPECT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, too_small.size(),
                too_small.data(), nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  retrieved_program.resize(size);
  ASSERT_EQ(IQM_QDMI_device_job_query_property(
                job, QDMI_DEVICE_JOB_PROPERTY_PROGRAM, retrieved_program.size(),
                retrieved_program.data(), nullptr),
            QDMI_SUCCESS);
  EXPECT_TRUE(std::ranges::equal(retrieved_program, latest_program));
}

TEST_F(DeviceJobMockTest, JobSubmissionWithoutRequiredParameters) {
  EXPECT_EQ(IQM_QDMI_device_job_submit(nullptr), QDMI_ERROR_INVALIDARGUMENT);

  // Test submitting job without required parameters
  EXPECT_EQ(IQM_QDMI_device_job_submit(job), QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceJobMockTest, JobStatusOperations) {
  // Test null pointer cases
  QDMI_Job_Status status{};
  EXPECT_EQ(IQM_QDMI_device_job_check(nullptr, &status),
            QDMI_ERROR_INVALIDARGUMENT);
  EXPECT_EQ(IQM_QDMI_device_job_check(job, nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test wait with null pointer
  EXPECT_EQ(IQM_QDMI_device_job_wait(nullptr, 10), QDMI_ERROR_INVALIDARGUMENT);

  // Test cancel with null pointer
  EXPECT_EQ(IQM_QDMI_device_job_cancel(nullptr), QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceJobMockTest, ResultRetrievalErrorCases) {
  size_t size_ret{};

  // Test null job parameter
  EXPECT_EQ(IQM_QDMI_device_job_get_results(nullptr, QDMI_JOB_RESULT_HIST_KEYS,
                                            0, nullptr, &size_ret),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test invalid result type
  EXPECT_EQ(IQM_QDMI_device_job_get_results(
                // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                job, static_cast<QDMI_Job_Result>(999), 0, nullptr, &size_ret),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test buffer too small using std::array
  std::array<char, 1> small_buffer{};
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 1,
                                            small_buffer.data(), nullptr),
            QDMI_ERROR_INVALIDARGUMENT);

  // Test getting result size for unfinished jobs
  EXPECT_EQ(IQM_QDMI_device_job_get_results(job, QDMI_JOB_RESULT_HIST_KEYS, 0,
                                            nullptr, &size_ret),
            QDMI_ERROR_INVALIDARGUMENT);
}

TEST_F(DeviceJobMockTest, MalformedCircuitHandling) {
  // Test with invalid JSON circuit
  const auto *const invalid_circuit = R"({"invalid": json})";
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(invalid_circuit) + 1, invalid_circuit),
            QDMI_SUCCESS);

  // Test with empty circuit
  const auto *const empty_circuit = "";
  EXPECT_EQ(IQM_QDMI_device_job_set_parameter(
                job, QDMI_DEVICE_JOB_PARAMETER_PROGRAM,
                strlen(empty_circuit) + 1, empty_circuit),
            QDMI_SUCCESS);
}

TEST_F(DeviceJobMockTest, EdgeCaseParameterValues) {
  // Test with zero shots
  constexpr size_t zero_shots = 0;
  EXPECT_EQ(
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM,
                                        sizeof(zero_shots), &zero_shots),
      QDMI_SUCCESS);

  // Test with very large shots number
  constexpr size_t large_shots = 1000000;
  EXPECT_EQ(
      IQM_QDMI_device_job_set_parameter(job, QDMI_DEVICE_JOB_PARAMETER_SHOTSNUM,
                                        sizeof(large_shots), &large_shots),
      QDMI_SUCCESS);
}
