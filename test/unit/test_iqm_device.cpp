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

#include "http_client.hpp"
#include "iqm_qdmi/device.h"
#include "logging.hpp"
#include "mock_http_client.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using testing::_; // NOLINT(bugprone-reserved-identifier)
using testing::DoAll;
using testing::Return;
using testing::SetArgReferee;

// Forward declaration of the internal init function
int IQM_QDMI_device_session_init_with_http_client(
    IQM_QDMI_Device_Session session,
    std::unique_ptr<iqm::IHttpClient> http_client);

// ============================================================================
// TEST FIXTURES
// ============================================================================

class DeviceTest : public testing::Test {
protected:
  IQM_QDMI_Device_Session session = nullptr;

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
  std::unique_ptr<iqm::MockHttpClient> mock_http_client_;
  iqm::MockHttpClient *mock_handle_{};

  void SetUp() override {
    EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
    mock_http_client_ = std::make_unique<iqm::MockHttpClient>();
    mock_handle_ = mock_http_client_.get();
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

  // Mock initialization responses for unified API
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
    DeviceIntegrationMockTest::SetUp();

    EXPECT_CALL(*mock_http_client_, get(_, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(list_quantum_computers_response),
                        Return(QDMI_SUCCESS)))
        .WillOnce(
            DoAll(SetArgReferee<2>(get_static_quantum_architectures_response),
                  Return(QDMI_SUCCESS)))
        .WillOnce(
            DoAll(SetArgReferee<2>(get_dynamic_quantum_architectures_response),
                  Return(QDMI_SUCCESS)))
        .WillOnce(DoAll(
            SetArgReferee<2>(get_calibration_set_quality_metrics_response),
            Return(QDMI_SUCCESS)))
        .WillOnce(DoAll(SetArgReferee<2>(cocos_health_response),
                        Return(QDMI_SUCCESS)));

    ASSERT_EQ(IQM_QDMI_device_session_init_with_http_client(
                  session, std::move(mock_http_client_)),
              QDMI_SUCCESS);

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

// ============================================================================
// UNIT TESTS - Basic functionality without external dependencies
// ============================================================================

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
                session, QDMI_DEVICE_SESSION_PARAMETER_CUSTOM3, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
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

// ============================================================================
// INTEGRATION MOCK TESTS - Full lifecycle with mocked dependencies
// ============================================================================

TEST_F(DeviceJobMockTest, FullLifecycle) {
  const std::string job_submission_response = R"({"id": "job-123"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_results_response =
      R"([{"counts": {"0": 100, "1": 0}}])";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_results_response), Return(QDMI_SUCCESS)));

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

TEST_F(DeviceJobMockTest, RetrieveShotMeasurements) {
  const std::string job_submission_response = R"({"id": "job-456"})";
  const std::string job_status_response = R"({"status": "ready"})";
  const std::string job_measurements_response =
      R"([{"meas_2_0_0": [[0], [1], [0], [1]], "meas_2_0_1": [[0], [0], [1], [1]]}])";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)))
      .WillOnce(DoAll(SetArgReferee<2>(job_measurements_response),
                      Return(QDMI_SUCCESS)));

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

  // This should NOT trigger another API call (mock expects only 2 GET calls)
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

TEST_F(DeviceJobMockTest, RetrieveShotsBeforeCompletion) {
  const std::string job_submission_response = R"({"id": "job-789"})";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));

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
  const std::string job_measurements_response = R"([{"m": [[0], [0]]}])";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)))
      .WillOnce(DoAll(SetArgReferee<2>(job_measurements_response),
                      Return(QDMI_SUCCESS)));

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
  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response_string_queue),
                      Return(QDMI_SUCCESS)));
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

TEST_F(DeviceJobMockTest, RejectNonIntegerMeasurementValues) {
  const std::string job_submission_response = R"({"id": "job-999"})";
  const std::string job_status_response = R"({"status": "ready"})";
  // Invalid: qubit result value is a string instead of an integer
  const std::string job_measurements_response = R"([{"meas_2_0_0": [["0"]]}])";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)))
      .WillOnce(DoAll(SetArgReferee<2>(job_measurements_response),
                      Return(QDMI_SUCCESS)));

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

TEST_F(DeviceJobMockTest, EmptyShotsReturnsNullTerminator) {
  const std::string job_submission_response = R"({"id": "job-empty"})";
  const std::string job_status_response = R"({"status": "ready"})";
  // Empty measurements array - no shots
  const std::string job_measurements_response = R"([])";

  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)))
      .WillOnce(DoAll(SetArgReferee<2>(job_measurements_response),
                      Return(QDMI_SUCCESS)));

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
  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(DoAll(SetArgReferee<2>(job_submission_response),
                      Return(QDMI_SUCCESS)));
  ret = IQM_QDMI_device_job_submit(job);
  ASSERT_EQ(ret, QDMI_SUCCESS);

  const std::string job_status_response = R"({"status": "ready"})";
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_status_response), Return(QDMI_SUCCESS)));
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
  EXPECT_CALL(*mock_handle_, get(_, _, _))
      .WillOnce(
          DoAll(SetArgReferee<2>(job_results_response), Return(QDMI_SUCCESS)))
      .WillOnce(
          DoAll(SetArgReferee<2>(get_dynamic_quantum_architectures_response),
                Return(QDMI_SUCCESS)))
      .WillOnce(
          DoAll(SetArgReferee<2>(get_calibration_set_quality_metrics_response),
                Return(QDMI_SUCCESS)));

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

  // Test network timeout
  EXPECT_CALL(*mock_http_client_, get(_, _, _))
      .WillOnce(Return(QDMI_ERROR_TIMEOUT));

  EXPECT_EQ(IQM_QDMI_device_session_init_with_http_client(
                session, std::move(mock_http_client_)),
            QDMI_ERROR_TIMEOUT);
}

TEST_F(DeviceIntegrationMockTest, HTTPAuthenticationFailure) {

  // Test authentication failure
  EXPECT_CALL(*mock_http_client_, get(_, _, _))
      .WillOnce(Return(QDMI_ERROR_PERMISSIONDENIED));

  EXPECT_EQ(IQM_QDMI_device_session_init_with_http_client(
                session, std::move(mock_http_client_)),
            QDMI_ERROR_PERMISSIONDENIED);
}

TEST_F(DeviceIntegrationMockTest, MalformedJSONResponse) {

  // Test malformed JSON responses
  EXPECT_CALL(*mock_http_client_, get(_, _, _))
      .WillOnce(DoAll(SetArgReferee<2>("invalid json"), Return(QDMI_SUCCESS)));

  // Expect a JSON parsing exception to be thrown
  EXPECT_THROW(IQM_QDMI_device_session_init_with_http_client(
                   session, std::move(mock_http_client_)),
               std::exception);
}

TEST_F(DeviceJobMockTest, DoubleInitializationPrevention) {
  mock_http_client_ = std::make_unique<iqm::MockHttpClient>();
  EXPECT_EQ(IQM_QDMI_device_session_init_with_http_client(
                session, std::move(mock_http_client_)),
            QDMI_ERROR_BADSTATE);
}

TEST_F(DeviceJobMockTest, JobSubmissionFailure) {
  // Mock job submission failure
  EXPECT_CALL(*mock_handle_, post(_, _, _, _, _))
      .WillOnce(Return(QDMI_ERROR_FATAL));

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

TEST_F(DeviceJobMockTest, JobSubmissionWithoutRequiredParameters) {
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
