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

#include "http_client.hpp"

#include "cpr/response.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {
// Global operator new observes these allocation-free controls in this isolated
// process. NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
int allocations_before_failure = -1;
int post_count = 0;
bool fail_after_confirmed_id = false;
const IQM_QDMI_Iteration_Result *watched_result = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
constexpr const char *ID = "12345678-1234-1234-abcd-123456789abc";
constexpr const char *OTHER_ID = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
} // namespace

// Isolated executable: allocator fault injection never enters the unit suite.
// Replacement allocation functions must allocate and release raw storage.
// NOLINTBEGIN(cppcoreguidelines-no-malloc)
void *operator new(const std::size_t size) {
  if (fail_after_confirmed_id && watched_result != nullptr &&
      std::strcmp(watched_result->iteration_id, ID) == 0) {
    fail_after_confirmed_id = false;
    throw std::bad_alloc{};
  }
  if (allocations_before_failure == 0) {
    allocations_before_failure = -1;
    throw std::bad_alloc{};
  }
  if (allocations_before_failure > 0) {
    --allocations_before_failure;
  }
  if (auto *memory = std::malloc(size == 0 ? 1 : size)) {
    return memory;
  }
  throw std::bad_alloc{};
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, [[maybe_unused]] std::size_t size) noexcept {
  std::free(memory);
}
void *operator new[](const std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory,
                       [[maybe_unused]] std::size_t size) noexcept {
  std::free(memory);
}
// NOLINTEND(cppcoreguidelines-no-malloc)

int main() {
  IQM_QDMI_Iteration_Client client = nullptr;
  if (IQM_QDMI_iteration_client_alloc("https://example.test", "test-token",
                                      nullptr, 30000,
                                      &client) != QDMI_SUCCESS) {
    return 1;
  }
  const auto respond = [](const auto &...) {
    ++post_count;
    cpr::Response response;
    response.status_code = 200;
    response.text =
        R"({"id":"12345678-1234-1234-abcd-123456789abc","status":"future-state-requiring-diagnostics"})";
    return response;
  };
  iqm::http::internal::Get_hooks().post = respond;
  iqm::http::internal::Get_hooks().get = respond;
  const std::string qc(128, 'q');
  IQM_QDMI_Iteration_Result result{};
  std::strcpy(result.iteration_id, OTHER_ID);
  std::strcpy(result.state, "completed");
  result.http_status = 200;
  allocations_before_failure = 0;
  auto status =
      IQM_QDMI_iteration_create(client, qc.c_str(), 60, 0, 30000, &result);
  allocations_before_failure = -1;
  if (status != QDMI_ERROR_OUTOFMEM || *result.iteration_id != '\0' ||
      *result.state != '\0' || result.http_status != 0) {
    return 2;
  }

  for (const auto operation :
       {IQM_QDMI_iteration_get, IQM_QDMI_iteration_end}) {
    for (const bool alias : {false, true}) {
      std::strcpy(result.iteration_id, alias ? ID : OTHER_ID);
      std::strcpy(result.state, "completed");
      result.http_status = 200;
      allocations_before_failure = 0;
      status =
          operation(client, alias ? result.iteration_id : ID, 30000, &result);
      allocations_before_failure = -1;
      if (status != QDMI_ERROR_OUTOFMEM ||
          std::strcmp(result.iteration_id, ID) != 0 || *result.state != '\0' ||
          result.http_status != 0) {
        return 3;
      }
    }
  }

  if (post_count != 0) {
    return 7;
  }

  // Fail only after ownership was copied, before unknown-state diagnostics.
  std::strcpy(result.iteration_id, OTHER_ID);
  watched_result = &result;
  fail_after_confirmed_id = true;
  status = IQM_QDMI_iteration_create(client, qc.c_str(), 60, 0, 30000, &result);
  fail_after_confirmed_id = false;
  if (status != QDMI_ERROR_OUTOFMEM ||
      std::strcmp(result.iteration_id, ID) != 0 || result.http_status != 200 ||
      post_count != 1) {
    return 4;
  }
  iqm::http::internal::Reset_hooks();
  IQM_QDMI_iteration_client_free(client);
  return 0;
}
