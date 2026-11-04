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

#include "iqm_qdmi/constants.h"
#include "iqm_qdmi/iteration.h"

// Deliberately compiled as C; the shared CI linter also inspects it as C++.
// NOLINTBEGIN(modernize-deprecated-headers,modernize-use-nullptr,modernize-use-designated-initializers,modernize-redundant-void-arg)
#include <stddef.h>

int main(void) {
  IQM_QDMI_Iteration_Client client = NULL;
  IQM_QDMI_Iteration_Result result = {0};
  if (IQM_QDMI_iteration_client_alloc("https://example.test", "test-token",
                                      NULL, 30000, &client) != QDMI_SUCCESS) {
    return 1;
  }
  if (IQM_QDMI_iteration_create(client, "qc", 60, 0, 30000, NULL) !=
      QDMI_ERROR_INVALIDARGUMENT) {
    return 2;
  }
  if (IQM_QDMI_iteration_get(client, "invalid", 30000, &result) !=
      QDMI_ERROR_INVALIDARGUMENT) {
    return 3;
  }
  if (IQM_QDMI_iteration_end(client, "invalid", 30000, &result) !=
      QDMI_ERROR_INVALIDARGUMENT) {
    return 4;
  }
  if (IQM_QDMI_iteration_client_diagnostics(client) == NULL) {
    return 5;
  }
  IQM_QDMI_iteration_client_free(client);
  return 0;
}
// NOLINTEND(modernize-deprecated-headers,modernize-use-nullptr,modernize-use-designated-initializers,modernize-redundant-void-arg)
