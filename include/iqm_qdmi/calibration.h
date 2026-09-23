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
 * @brief IQM-specific calibration job submission.
 */

#pragma once

#include "iqm_qdmi/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Submit an IQM calibration job.
 * @details Create the job with IQM_QDMI_device_session_create_device_job and
 * set QDMI_DEVICE_JOB_PARAMETER_PROGRAM to an IQM Server calibration
 * configuration encoded as a JSON string. A terminating null byte is optional.
 * The program format, shot count, and circuit-specific parameters are ignored.
 * Calibration support is checked during session initialization.
 *
 * On success, use the normal QDMI job check, wait, cancel, and free functions.
 * QDMI_JOB_RESULT_CUSTOM1 returns the new calibration set ID and refreshes the
 * session's calibration data, invalidating previously queried operation
 * handles. Program-format and shot-count queries return
 * QDMI_ERROR_NOTSUPPORTED.
 *
 * @param job An unsubmitted job belonging to an initialized IQM session.
 * @return QDMI_SUCCESS if the job was submitted; QDMI_ERROR_INVALIDARGUMENT
 * for a null job or an unset program; QDMI_ERROR_BADSTATE for a job that is not
 * in QDMI_JOB_STATUS_CREATED; QDMI_ERROR_NOTSUPPORTED if calibration is
 * unavailable; QDMI_ERROR_PERMISSIONDENIED for an authentication failure;
 * QDMI_ERROR_OUTOFMEM for an allocation failure; QDMI_ERROR_FATAL otherwise.
 */
IQM_QDMI_EXPORT int
IQM_QDMI_device_job_submit_calibration(IQM_QDMI_Device_Job job);

#ifdef __cplusplus
}
#endif
