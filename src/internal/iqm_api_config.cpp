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
 * @brief Implementation of the IQM QDMI Device API Configuration.
 */

#include "iqm_api_config.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace iqm {
namespace {
// NOLINTNEXTLINE(*-throwing-static-initialization)
const std::unordered_map<API_ENDPOINT, std::string> URLS = {
    // Job management endpoints
    {API_ENDPOINT::SUBMIT_CIRCUIT_JOB, "api/v1/jobs/%s/circuit"},
    {API_ENDPOINT::GET_JOB_STATUS, "api/v1/jobs/%s"},
    {API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENT_COUNTS,
     "api/v1/jobs/%s/artifacts/measurement_counts"},
    {API_ENDPOINT::GET_JOB_ARTIFACT_MEASUREMENTS,
     "api/v1/jobs/%s/artifacts/measurements"},
    {API_ENDPOINT::CANCEL_JOB, "api/v1/jobs/%s/cancel"},

    // Quantum computer endpoints
    {API_ENDPOINT::GET_QUANTUM_COMPUTERS, "api/v1/quantum-computers"},
    {API_ENDPOINT::GET_STATIC_QUANTUM_ARCHITECTURE,
     "api/v1/quantum-computers/%s/artifacts/static-quantum-architectures"},
    {API_ENDPOINT::GET_DYNAMIC_QUANTUM_ARCHITECTURE,
     "api/v1/calibration-sets/%s/%s/dynamic-quantum-architecture"},
    {API_ENDPOINT::GET_CALIBRATION_SET_QUALITY_METRICS,
     "api/v1/calibration-sets/%s/%s/metrics"},

    // Calibration job endpoints (legacy - may not be available)
    {API_ENDPOINT::COCOS_HEALTH, "cocos/health"},
    {API_ENDPOINT::SUBMIT_CALIBRATION_JOB, "cocos/api/v4/calibration/runs"},
    {API_ENDPOINT::GET_CALIBRATION_JOB_STATUS,
     "cocos/api/v4/calibration/runs/%s/status"},
    {API_ENDPOINT::ABORT_CALIBRATION_JOB,
     "cocos/api/v4/calibration/runs/%s/abort"}};
} // namespace

APIConfig::APIConfig(std::string iqm_server_url)
    : iqm_server_url_(std::move(iqm_server_url)) {}

std::string APIConfig::get_url(const API_ENDPOINT endpoint) {
  return URLS.at(endpoint);
}

std::string APIConfig::join_paths(const std::string &base,
                                  const std::string &path) {
  if (base.empty()) {
    return path;
  }

  std::string result = base;

  // Ensure base ends with a slash
  if (result.back() != '/') {
    result += '/';
  }

  // Remove leading slash from path if present
  if (!path.empty() && path.front() == '/') {
    result += path.substr(1);
  } else {
    result += path;
  }

  return result;
}

} // namespace iqm
