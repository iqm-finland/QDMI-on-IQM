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

/** @file
 * @brief Hermetic device sessions shared by unit and query conformance tests.
 */

#pragma once

#include "http_stub.hpp"
#include "iqm_qdmi/device.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <utility>

inline int Set_env_var_raw(const char *key, const char *value) {
#ifdef _WIN32
  return _putenv_s(key, value);
#else
  // NOLINTNEXTLINE(misc-include-cleaner)
  return setenv(key, value, 1);
#endif
}

inline int Unset_env_var_raw(const char *key) {
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

class DeviceIntegrationMockTest : public testing::Test {
public:
  static void SetUpTestSuite() {
    ASSERT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
  }

  static void TearDownTestSuite() {
    EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
  }

protected:
  IQM_QDMI_Device_Session session = nullptr;
  iqm::test_support::HttpStub http_stub;

private:
  ScopedUnsetEnvVar server_url_env_{"IQM_SERVER_URL"};
  ScopedUnsetEnvVar base_url_env_{"IQM_BASE_URL"};
  ScopedUnsetEnvVar token_env_{"IQM_TOKEN"};
  ScopedUnsetEnvVar tokens_file_env_{"IQM_TOKENS_FILE"};
  ScopedUnsetEnvVar qc_id_env_{"IQM_QC_ID"};
  ScopedUnsetEnvVar qc_alias_env_{"IQM_QC_ALIAS"};
  ScopedUnsetEnvVar quantum_computer_env_{"IQM_QUANTUM_COMPUTER"};

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
    ASSERT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);
    const std::string base_url = "https://localhost";
    ASSERT_EQ(IQM_QDMI_device_session_set_parameter(
                  session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL,
                  base_url.size() + 1, base_url.c_str()),
              QDMI_SUCCESS);
  }

  void TearDown() override {
    if (session != nullptr) {
      IQM_QDMI_device_session_free(session);
    }
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
            "angle": 1.5707963267948966,
            "phase": 4.71238898038469
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

class QDMIQueryFixture : public DeviceIntegrationMockTest {
protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(DeviceIntegrationMockTest::SetUp());
    queue_successful_initialization();
    ASSERT_EQ(IQM_QDMI_device_session_init(session), QDMI_SUCCESS);
    // Both status endpoints and queue queries receive stable local metadata.
    for (int i = 0; i < 16; ++i) {
      http_stub.queue_get(
          200, R"({"healthy":true,"queue_length":0,"available":[{}]})");
    }
  }
};
