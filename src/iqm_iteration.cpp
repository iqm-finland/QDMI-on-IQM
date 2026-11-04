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
 * @brief Standalone IQM iteration management.
 */
#include "http_client.hpp"
#include "iqm_api_config.hpp"
#include "iqm_auth.hpp"
#include "iqm_qdmi/iteration.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cpr/error.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct IQM_QDMI_Iteration_Client_impl {
  iqm::TokenManager tokens_;
  iqm::APIConfig api_;
  cpr::ConnectionPool pool_;
  iqm::http::Rate_limit_budget quota_;
  std::chrono::milliseconds request_timeout_;
  std::string diagnostics_ = "{}";
  const char *fallback_ = nullptr;
  std::array<char, 192> emergency_{};
  bool creation_uncertain_ = false;

  IQM_QDMI_Iteration_Client_impl(std::string url,
                                 const std::optional<std::string> &token,
                                 const std::optional<std::string> &file,
                                 const std::chrono::milliseconds timeout)
      : tokens_(token, file), api_(std::move(url)), request_timeout_(timeout) {}
};

namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *INVALID_ARGUMENT =
    R"({"http_status":0,"message":"Invalid argument"})";

bool Valid_timeout(const uint64_t timeout, const Clock::time_point now) {
  const auto available = std::chrono::duration_cast<std::chrono::milliseconds>(
                             Clock::time_point::max() - now)
                             .count();
  return timeout > 0 && available > 0 &&
         timeout <= static_cast<uint64_t>(available);
}

bool Normalize_uuid(std::string &id) {
  if (id.size() != 36)
    return false;
  for (size_t i = 0; i < id.size(); ++i) {
    auto &c = id[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-')
        return false;
    } else {
      if (c >= 'A' && c <= 'F')
        c = static_cast<char>(c - 'A' + 'a');
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
    }
  }
  return true;
}

std::optional<std::string> Optional_string(const char *value) {
  return value == nullptr ? std::nullopt : std::optional<std::string>{value};
}

void Diagnostic(IQM_QDMI_Iteration_Client client, const int http_status,
                const char *message) {
  auto json =
      nlohmann::json{{"http_status", http_status}, {"message", message}};
  if (client->creation_uncertain_)
    json["creation_uncertain"] = true;
  client->diagnostics_ = json.dump();
}

void Emergency(IQM_QDMI_Iteration_Client client,
               const IQM_QDMI_Iteration_Result *result,
               const char *message) noexcept {
  if (client == nullptr)
    return;
  std::snprintf(client->emergency_.data(), client->emergency_.size(),
                R"({"http_status":%d,"message":"%s"%s})",
                result == nullptr ? 0 : result->http_status, message,
                client->creation_uncertain_ ? R"(,"creation_uncertain":true)"
                                            : "");
  client->fallback_ = client->emergency_.data();
}

template <typename Operation>
int Boundary(IQM_QDMI_Iteration_Client client,
             IQM_QDMI_Iteration_Result *result, Operation operation) noexcept {
  try {
    if (client != nullptr) {
      client->fallback_ = nullptr;
      client->creation_uncertain_ = false;
      client->diagnostics_ = "{}";
    }
    return operation();
  } catch (const std::bad_alloc &) {
    Emergency(client, result, "Allocation failure");
    return QDMI_ERROR_OUTOFMEM;
  } catch (const iqm::ClientAuthenticationError &) {
    Emergency(client, result, "Authentication failed");
    return QDMI_ERROR_PERMISSIONDENIED;
  } catch (...) {
    Emergency(client, result, "Client failure");
    return QDMI_ERROR_FATAL;
  }
}

int Request(IQM_QDMI_Iteration_Client client, const iqm::API_ENDPOINT endpoint,
            const std::string &id, const std::string &body,
            const Clock::time_point deadline,
            IQM_QDMI_Iteration_Result &result) {
  const bool create = endpoint == iqm::API_ENDPOINT::CREATE_ITERATION;
  const auto url = client->api_.url(endpoint, id);
  const auto bearer = client->tokens_.get_bearer_token();
  client->creation_uncertain_ = create;
  const auto response =
      endpoint == iqm::API_ENDPOINT::GET_ITERATION
          ? iqm::http::Get(url, bearer, client->pool_, client->request_timeout_,
                           deadline, &client->quota_)
          : iqm::http::Post(url, bearer, client->pool_, body,
                            client->request_timeout_, deadline,
                            &client->quota_);
  result.http_status = static_cast<int>(response.status_code);
  const bool success = response.status_code == 200 ||
                       (endpoint == iqm::API_ENDPOINT::END_ITERATION &&
                        response.status_code == 202);
  // Explicit refusals are safe; transport/5xx/redirect outcomes remain
  // uncertain.
  if (create && !response.error && response.status_code >= 400 &&
      response.status_code < 500)
    client->creation_uncertain_ = false;
  auto json = nlohmann::json::parse(response.text, nullptr, false);
  bool valid = false;
  const bool received_success =
      response.status_code >= 200 && response.status_code < 300;
  std::string protocol_error;
  if (!json.is_discarded() && json.is_object() && received_success) {
    if (const auto it = json.find("id"); it != json.end() && it->is_string()) {
      auto returned_id = it->get<std::string>();
      if (Normalize_uuid(returned_id) && (create || returned_id == id)) {
        // Retain ownership before decoding state or allocating diagnostics.
        std::memcpy(result.iteration_id, returned_id.c_str(),
                    returned_id.size() + 1);
        client->creation_uncertain_ = false;
        const auto state = json.find("status");
        if (state != json.end() && state->is_string()) {
          const auto &text = state->get_ref<const std::string &>();
          constexpr std::array known{"waiting", "active", "completed",
                                     "cancelled", "failed"};
          if (std::ranges::find(known, text) != known.end()) {
            std::memcpy(result.state, text.c_str(), text.size() + 1);
            valid = true;
          } else
            protocol_error = "Unknown iteration state: " + text;
        }
      }
    }
    if (!valid && protocol_error.empty())
      protocol_error = "Malformed iteration response or mismatching ID";
  } else if (received_success)
    protocol_error = "Malformed iteration response";
  if (received_success && !success)
    protocol_error = "Unexpected HTTP success status";

  auto diagnostics = nlohmann::json{{"http_status", result.http_status}};
  if (json.is_object()) {
    for (const auto *key : {"error_code", "message", "request_id"}) {
      if (const auto field = json.find(key);
          field != json.end() && field->is_string()) {
        diagnostics[key] = *field;
      }
    }
  }
  if (!protocol_error.empty())
    diagnostics["message"] = protocol_error;
  if (bearer.has_value()) {
    const std::string_view secret{bearer->GetToken()};
    if (!secret.empty()) {
      for (auto &value : diagnostics) {
        if (!value.is_string())
          continue;
        auto &text = value.get_ref<std::string &>();
        size_t pos = 0;
        while ((pos = text.find(secret, pos)) != std::string::npos) {
          text.replace(pos, secret.size(), "[redacted]");
          pos += std::string_view{"[redacted]"}.size();
        }
      }
    }
  }
  if (client->creation_uncertain_)
    diagnostics["creation_uncertain"] = true;
  client->diagnostics_ = diagnostics.dump();
  if (response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT)
    return QDMI_ERROR_TIMEOUT;
  if (response.error)
    return QDMI_ERROR_FATAL;
  if (received_success)
    return success && valid ? QDMI_SUCCESS : QDMI_ERROR_FATAL;
  // Only selected diagnostics reach the existing logger; never log raw bodies.
  cpr::Response sanitized;
  sanitized.status_code = response.status_code;
  sanitized.url = url;
  sanitized.text = client->diagnostics_;
  return iqm::http::Handle_response(sanitized);
}

int Get_or_end(IQM_QDMI_Iteration_Client client, const char *iteration_id,
               const uint64_t timeout_ms, IQM_QDMI_Iteration_Result *result,
               const iqm::API_ENDPOINT endpoint) {
  return Boundary(client, result, [&]() -> int {
    const auto now = iqm::http::internal::Get_hooks().now();
    // Snapshot before clearing: iteration_id may point inside result.
    std::string id = iteration_id == nullptr ? "" : iteration_id;
    const auto original = id;
    const bool valid_id = Normalize_uuid(id) && id == original;
    if (result != nullptr) {
      *result = {};
      if (valid_id)
        std::memcpy(result->iteration_id, id.c_str(), id.size() + 1);
    }
    if (client == nullptr || result == nullptr || !valid_id ||
        !Valid_timeout(timeout_ms, now)) {
      if (client != nullptr)
        Diagnostic(client, 0, "Invalid ID, output or timeout");
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    return Request(client, endpoint, id, "",
                   now + std::chrono::milliseconds{timeout_ms}, *result);
  });
}
} // namespace

int IQM_QDMI_iteration_client_alloc(const char *base_url, const char *token,
                                    const char *tokens_file,
                                    const uint64_t request_timeout_ms,
                                    IQM_QDMI_Iteration_Client *client) {
  if (client == nullptr)
    return QDMI_ERROR_INVALIDARGUMENT;
  *client = nullptr;
  try {
    if (!Valid_timeout(request_timeout_ms,
                       iqm::http::internal::Get_hooks().now()))
      return QDMI_ERROR_INVALIDARGUMENT;
    std::string url = base_url == nullptr ? "" : base_url;
    if (base_url == nullptr) {
      for (const auto *key : {"IQM_SERVER_URL", "IQM_BASE_URL"}) {
        if (const auto *value = std::getenv(key);
            value != nullptr && *value != '\0') {
          url = value;
          break;
        }
      }
    }
    if (url.empty())
      return QDMI_ERROR_INVALIDARGUMENT;
    *client = new IQM_QDMI_Iteration_Client_impl(
        std::move(url), Optional_string(token), Optional_string(tokens_file),
        std::chrono::milliseconds{request_timeout_ms});
    return QDMI_SUCCESS;
  } catch (const std::bad_alloc &) {
    return QDMI_ERROR_OUTOFMEM;
  } catch (const iqm::ClientConfigurationError &) {
    return QDMI_ERROR_INVALIDARGUMENT;
  } catch (...) {
    return QDMI_ERROR_FATAL;
  }
}

void IQM_QDMI_iteration_client_free(IQM_QDMI_Iteration_Client client) {
  try {
    delete client;
  } catch (...) { /* No exception may cross the C ABI. */
  }
}

const char *
IQM_QDMI_iteration_client_diagnostics(IQM_QDMI_Iteration_Client client) {
  if (client == nullptr)
    return INVALID_ARGUMENT;
  return client->fallback_ != nullptr ? client->fallback_
                                      : client->diagnostics_.c_str();
}

int IQM_QDMI_iteration_create(IQM_QDMI_Iteration_Client client, const char *qc,
                              const uint32_t max_duration_seconds,
                              const uint32_t idle_timeout_seconds,
                              const uint64_t timeout_ms,
                              IQM_QDMI_Iteration_Result *result) {
  return Boundary(client, result, [&]() -> int {
    const auto now = iqm::http::internal::Get_hooks().now();
    const std::string selector = qc == nullptr ? "" : qc;
    if (result != nullptr)
      *result = {};
    if (client == nullptr || result == nullptr || selector.empty() ||
        max_duration_seconds < 1 || max_duration_seconds > 2700 ||
        idle_timeout_seconds > max_duration_seconds ||
        !Valid_timeout(timeout_ms, now)) {
      if (client != nullptr)
        Diagnostic(client, 0, "Invalid QC, duration, output or timeout");
      return QDMI_ERROR_INVALIDARGUMENT;
    }
    auto body = nlohmann::json{{"qc", selector},
                               {"max_duration_seconds", max_duration_seconds}};
    if (idle_timeout_seconds != 0)
      body["idle_timeout_seconds"] = idle_timeout_seconds;
    return Request(client, iqm::API_ENDPOINT::CREATE_ITERATION, "", body.dump(),
                   now + std::chrono::milliseconds{timeout_ms}, *result);
  });
}

int IQM_QDMI_iteration_get(IQM_QDMI_Iteration_Client client,
                           const char *iteration_id, const uint64_t timeout_ms,
                           IQM_QDMI_Iteration_Result *result) {
  return Get_or_end(client, iteration_id, timeout_ms, result,
                    iqm::API_ENDPOINT::GET_ITERATION);
}

int IQM_QDMI_iteration_end(IQM_QDMI_Iteration_Client client,
                           const char *iteration_id, const uint64_t timeout_ms,
                           IQM_QDMI_Iteration_Result *result) {
  return Get_or_end(client, iteration_id, timeout_ms, result,
                    iqm::API_ENDPOINT::END_ITERATION);
}
