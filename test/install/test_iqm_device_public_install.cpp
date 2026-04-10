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

#include "iqm_qdmi/device.h"

#include <gtest/gtest.h>

TEST(InstallPublicApiTest, InitializeAndFinalize) {
  EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
}

TEST(InstallPublicApiTest, SessionAllocateAndFree) {
  EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);

  IQM_QDMI_Device_Session session = nullptr;
  EXPECT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);
  EXPECT_NE(session, nullptr);

  IQM_QDMI_device_session_free(session);
  EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
}

TEST(InstallPublicApiTest, SessionParameterValidation) {
  EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);

  IQM_QDMI_Device_Session session = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);

  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, 6, "value"),
            QDMI_SUCCESS);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                session, QDMI_DEVICE_SESSION_PARAMETER_AUTHURL, 6, "value"),
            QDMI_ERROR_NOTSUPPORTED);
  EXPECT_EQ(IQM_QDMI_device_session_set_parameter(
                nullptr, QDMI_DEVICE_SESSION_PARAMETER_BASEURL, 6, "value"),
            QDMI_ERROR_INVALIDARGUMENT);

  IQM_QDMI_device_session_free(session);
  EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
}

TEST(InstallPublicApiTest, JobCreationWithoutInitReturnsBadState) {
  EXPECT_EQ(IQM_QDMI_device_initialize(), QDMI_SUCCESS);

  IQM_QDMI_Device_Session session = nullptr;
  ASSERT_EQ(IQM_QDMI_device_session_alloc(&session), QDMI_SUCCESS);

  IQM_QDMI_Device_Job job = nullptr;
  EXPECT_EQ(IQM_QDMI_device_session_create_device_job(session, &job),
            QDMI_ERROR_BADSTATE);

  IQM_QDMI_device_session_free(session);
  EXPECT_EQ(IQM_QDMI_device_finalize(), QDMI_SUCCESS);
}
