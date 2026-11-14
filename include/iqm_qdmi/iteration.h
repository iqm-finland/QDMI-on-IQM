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
 * @brief Standalone IQM Server iteration management, independent of sessions.
 */
#pragma once
#include "iqm_qdmi/device.h"

// This installed header must compile as both C and C++.
#include <stdint.h> // NOLINT(modernize-deprecated-headers)
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque, serially used client owning authentication and connections.
 */
typedef struct IQM_QDMI_Iteration_Client_impl *IQM_QDMI_Iteration_Client;

/** @brief Caller-owned output, retained even when an operation fails. */
typedef struct {
  char iteration_id[37]; /**< Canonical UUID, or empty for unconfirmed creation.
                          */
  char state[16];  /**< Known server state, or empty on decoding failure. */
  int http_status; /**< HTTP response status, or zero without a response. */
} IQM_QDMI_Iteration_Result;

/**
 * @brief Allocate without network requests, copying connection/auth inputs.
 * @details Null inputs use IQM_SERVER_URL (then IQM_BASE_URL) and TokenManager
 * defaults. Timeout must be positive and representable. Sets *client to null
 * on failure. Separate clients may run concurrently; calls on one are serial.
 * @return QDMI_SUCCESS, INVALIDARGUMENT, OUTOFMEM, or FATAL.
 */
IQM_QDMI_EXPORT int IQM_QDMI_iteration_client_alloc(
    const char *base_url, const char *token, const char *tokens_file,
    uint64_t request_timeout_ms, IQM_QDMI_Iteration_Client *client);

/** @brief Free resources only; never end an iteration. Null is a no-op. */
IQM_QDMI_EXPORT void
IQM_QDMI_iteration_client_free(IQM_QDMI_Iteration_Client client);

/**
 * @brief Borrow NUL-terminated JSON until the next operation or free.
 * @details Includes http_status and selected server error_code, message and
 * request_id. creation_uncertain marks ambiguous creation without a confirmed
 * ID. Null clients return a static invalid-argument diagnostic.
 */
IQM_QDMI_EXPORT const char *
IQM_QDMI_iteration_client_diagnostics(IQM_QDMI_Iteration_Client client);

/**
 * @brief Create for a nonempty QC UUID/alias and maximum of 1..2700 seconds.
 * @details Idle zero omits the field; otherwise require 1..maximum. Result is
 * mandatory (no size-query mode). Retain an ID even on partial decoding
 * failure. Never automatically retry ambiguous creation; reconcile through
 * server listing. All timeouts are positive milliseconds; the operation
 * deadline includes quota waits/retries and caps each HTTP request. Token-file
 * reads and response decoding are synchronous and can exceed it. Redirects
 * are not followed.
 * @return Existing QDMI status codes; diagnostics distinguish server reasons.
 */
IQM_QDMI_EXPORT int
IQM_QDMI_iteration_create(IQM_QDMI_Iteration_Client client, const char *qc,
                          uint32_t max_duration_seconds,
                          uint32_t idle_timeout_seconds, uint64_t timeout_ms,
                          IQM_QDMI_Iteration_Result *result);

/**
 * @brief Inspect a canonical UUID; result may alias iteration_id.
 * @details Retains the input ID on failure; rejects mismatching response IDs.
 * Known states are waiting, active, completed, cancelled, and failed. Unknown
 * states fail closed and remain in diagnostics unless redacted. Result is
 * mandatory.
 * @return Existing QDMI status codes, including TIMEOUT for local expiry.
 */
IQM_QDMI_EXPORT int IQM_QDMI_iteration_get(IQM_QDMI_Iteration_Client client,
                                           const char *iteration_id,
                                           uint64_t timeout_ms,
                                           IQM_QDMI_Iteration_Result *result);

/**
 * @brief End idempotently, returning the POST's 200 or 202 without polling.
 * @details The caller polls after 202 and owns cleanup. Arguments and failures
 * follow get. Freeing a client or device session never ends the iteration.
 * @return Existing QDMI status codes; http_status distinguishes 200 and 202.
 */
IQM_QDMI_EXPORT int IQM_QDMI_iteration_end(IQM_QDMI_Iteration_Client client,
                                           const char *iteration_id,
                                           uint64_t timeout_ms,
                                           IQM_QDMI_Iteration_Result *result);
#ifdef __cplusplus
}
#endif
