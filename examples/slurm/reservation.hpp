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

#pragma once

#include "iqm_qdmi/device.h"

#include <string>

namespace iqm::slurm {
void Require_success(int status, const std::string &operation);
/// Persist the reservation ID before submitting the first dependent circuit.
void Run_circuits(IQM_QDMI_Device_Session session, const std::string &scratch);
/// Remove each saved ID only after observing a terminal reservation state.
void Cleanup(IQM_QDMI_Device_Session session, const std::string &scratch);
} // namespace iqm::slurm
