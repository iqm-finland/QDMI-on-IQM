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

#include "iqm_api_config.hpp"

#include <gtest/gtest.h>

TEST(APIConfigTest, UnifiedApiUrl) {
  const iqm::APIConfig config("http://test.url");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::SUBMIT_CIRCUIT_JOB, "qc_id"),
            "http://test.url/api/v1/jobs/qc_id/circuit");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_JOB_STATUS, "123"),
            "http://test.url/api/v1/jobs/123");
  EXPECT_EQ(
      config.url(iqm::API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENT_COUNTS, "123"),
      "http://test.url/api/v1/jobs/123/artifacts/measurement_counts");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENTS, "123"),
            "http://test.url/api/v1/jobs/123/artifacts/measurements");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::CANCEL_JOB, "123"),
            "http://test.url/api/v1/jobs/123/cancel");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_QUANTUM_COMPUTERS),
            "http://test.url/api/v1/quantum-computers");
  EXPECT_EQ(
      config.url(iqm::API_ENDPOINT::GET_STATIC_QUANTUM_ARCHITECTURE, "qc_id"),
      "http://test.url/api/v1/quantum-computers/qc_id/artifacts/"
      "static-quantum-architectures");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_CALIBRATION_SET_QUALITY_METRICS,
                       "qc_id", "cal_set"),
            "http://test.url/api/v1/calibration-sets/qc_id/cal_set/metrics");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_DYNAMIC_QUANTUM_ARCHITECTURE,
                       "qc_id", "cal_set"),
            "http://test.url/api/v1/calibration-sets/qc_id/cal_set/"
            "dynamic-quantum-architecture");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::SUBMIT_CALIBRATION_JOB),
            "http://test.url/cocos/api/v4/calibration/runs");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::GET_CALIBRATION_JOB_STATUS, "run_id"),
            "http://test.url/cocos/api/v4/calibration/runs/run_id/status");
  EXPECT_EQ(config.url(iqm::API_ENDPOINT::ABORT_CALIBRATION_JOB, "run_id"),
            "http://test.url/cocos/api/v4/calibration/runs/run_id/abort");
}

TEST(APIConfigTest, JoinPaths) {
  const iqm::APIConfig config1("http://test.url");
  EXPECT_EQ(config1.url(iqm::API_ENDPOINT::GET_QUANTUM_COMPUTERS),
            "http://test.url/api/v1/quantum-computers");

  const iqm::APIConfig config2("http://test.url/");
  EXPECT_EQ(config2.url(iqm::API_ENDPOINT::GET_QUANTUM_COMPUTERS),
            "http://test.url/api/v1/quantum-computers");

  const iqm::APIConfig config3("");
  EXPECT_EQ(config3.url(iqm::API_ENDPOINT::GET_QUANTUM_COMPUTERS),
            "api/v1/quantum-computers");
}
