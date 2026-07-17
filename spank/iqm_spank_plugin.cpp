/*
 * Copyright (c) 2026 IQM Finland Oy
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/** @file
 * @brief Slurm SPANK plugin for IQM QDMI integration.
 *
 * This plugin provides HPC administrators with controlled injection of IQM
 * environment variables into Slurm jobs. Configuration is specified in
 * plugstack.conf and automatically propagated to the job environment on the
 * compute node. Users can also override values via srun command-line options.
 *
 * The plugin can be restricted to specific partitions via the `partitions=`
 * plugstack argument. When set, the plugin is a no-op on non-matching
 * partitions.
 *
 * Configuration precedence (highest first):
 *   1. srun command-line options (--iqm-base-url, --iqm-tokens-file, etc.)
 *   2. User-set environment variables
 *   3. plugstack.conf defaults
 *
 * Example plugstack.conf entry:
 * @code
 * required /usr/lib/slurm/iqm-spank-plugin.so \
 *     partitions=quantum                           \
 *     iqm_base_url=https://resonance.iqm.tech      \
 *     iqm_tokens_file=/etc/iqm/tokens.json         \
 *     iqm_qc_alias=emerald
 * @endcode
 *
 * Supported plugstack arguments, srun options, and mapped env variables:
 *   - iqm_base_url     /  --iqm-base-url     → IQM_BASE_URL
 *   - iqm_tokens_file  /  --iqm-tokens-file  → IQM_TOKENS_FILE
 *   - iqm_qc_id        /  --iqm-qc-id        → IQM_QC_ID
 *   - iqm_qc_alias     /  --iqm-qc-alias     → IQM_QC_ALIAS
 *
 * Additional plugstack-only arguments:
 *   - partitions=quantum,quantum-dev (comma-separated, restricts activation)
 *   - iqm_license_prefix=iqm_qc_ (prefix used to derive the expected Slurm
 *     license name from IQM_QC_ALIAS; default: "iqm_qc_")
 *   - iqm_require_license=1 (reject jobs whose Slurm license request does
 *     not match the derived name; default: off, warn only)
 *
 * Partition detection uses the `SLURM_JOB_PARTITION` environment variable
 * (set by Slurm in every job), which is portable across all Slurm versions.
 *
 * When `IQM_QC_ALIAS` is resolved, the plugin can optionally check that the
 * job's Slurm license request (the native `--licenses`/`-L` submission
 * option, surfaced in the job environment as `SLURM_JOB_LICENSES`) matches
 * `<iqm_license_prefix><alias>`. This is a *Slurm* license — a
 * capacity-limiting scheduler resource configured via `Licenses=` in
 * slurm.conf, unrelated to GPLv3/software licensing — used to bound
 * concurrent access to a QC. This matters most for on-premise QCs: like
 * Resonance, they still run their own internal queue, but typically front
 * single-tenant hardware, so uncontrolled Slurm-side concurrency puts
 * unnecessary pressure on that queue. Since aliases may contain characters
 * reserved by Slurm's `name:count` license syntax (e.g. the `:mock` suffix used
 * to select simulator variants, as in `emerald:mock`), `:` and `,` are replaced
 * with `_` when deriving the expected license name. By default a mismatch
 * only logs a warning; `iqm_require_license=1` fails the task at launch
 * instead (this happens in `slurm_spank_task_init`, i.e. *after* the job has
 * already been allocated — it is not a submission-time rejection — and only
 * takes effect if the plugin is declared `required`, not `optional`, in
 * plugstack.conf). This check requires Slurm >= 23.02 (`SLURM_JOB_LICENSES`);
 * on older Slurm, or when the job has no alias, it is a silent no-op.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// Slurm's SPANK API (slurm_spank_log, slurm_debug) uses C-style vararg
// functions. This is unavoidable when interfacing with the Slurm C API.

extern "C" {
#include <slurm/slurm_errno.h>
#include <slurm/slurm_version.h>
#include <slurm/spank.h>
}

namespace {

/// A single mapping from a plugstack argument key, srun option, and
/// environment variable.
struct Config_mapping {
  /// Key expected in plugstack.conf (e.g. "iqm_base_url").
  std::string_view key;
  /// Environment variable to set in the job (e.g. "IQM_BASE_URL").
  std::string_view env_var;
  /// srun option long name (e.g. "iqm-base-url").
  const char *option_name;
  /// srun option usage description.
  const char *option_usage;
};

/// All recognized configuration entries and their mappings.
constexpr std::array<Config_mapping, 4> K_CONFIG_MAPPINGS = {{
    {.key = "iqm_base_url",
     .env_var = "IQM_BASE_URL",
     .option_name = "iqm-base-url",
     .option_usage = "IQM server base URL"},
    {.key = "iqm_tokens_file",
     .env_var = "IQM_TOKENS_FILE",
     .option_name = "iqm-tokens-file",
     .option_usage = "Path to IQM tokens file"},
    {.key = "iqm_qc_id",
     .env_var = "IQM_QC_ID",
     .option_name = "iqm-qc-id",
     .option_usage = "Quantum computer ID"},
    {.key = "iqm_qc_alias",
     .env_var = "IQM_QC_ALIAS",
     .option_name = "iqm-qc-alias",
     .option_usage = "Quantum computer alias"},
}};

/**
 * @brief Parses plugstack.conf arguments, processes srun options, and injects
 * IQM environment variables into Slurm jobs.
 *
 * Lifecycle:
 *   1. `parse_plugstack_args()` is called from `slurm_spank_init` to extract
 *      `key=value` pairs from the plugstack.conf line.
 *   2. `register_options()` is called from `slurm_spank_init` to register
 *      `--iqm-*` srun command-line options.
 *   3. `inject_environment()` is called from `slurm_spank_task_init` (remote
 *      context) to set environment variables in the job via `spank_setenv()`.
 *   4. `validate_environment()` is called after injection to perform
 *      superficial configuration validation. Deeper validation is deferred to
 *      the IQM QDMI device implementation.
 *   5. `validate_license_alignment()` is called after `validate_environment()`
 *      to check that the job's Slurm license request matches the targeted
 *      QC alias.
 */
class IQMSpankConfigManager final {
public:
  /**
   * @brief Parse plugstack.conf arguments (key=value pairs).
   * @param ac Argument count from the SPANK hook.
   * @param av Argument vector from the SPANK hook.
   */
  void parse_plugstack_args(const int ac, char **av) {
    for (int i = 0; i < ac; ++i) {
      if (av[i] == nullptr) {
        continue;
      }
      const std::string_view arg{av[i]};
      const auto eq_pos = arg.find('=');
      if (eq_pos == std::string_view::npos || eq_pos == 0 ||
          eq_pos + 1 >= arg.size()) {
        slurm_spank_log(
            "[iqm_spank_plugin] warning: ignoring malformed plugstack "
            "argument '%s' (expected key=value)",
            av[i]);
        continue;
      }
      const auto key = arg.substr(0, eq_pos);
      const auto value = arg.substr(eq_pos + 1);

      bool matched = false;
      for (std::size_t j = 0; j < K_CONFIG_MAPPINGS.size(); ++j) {
        if (key == K_CONFIG_MAPPINGS[j].key) {
          plugstack_values_[j] = std::string{value};
          matched = true;
          break;
        }
      }

      // Handle the partition filter argument.
      if (!matched && key == "partitions") {
        parse_partition_list(value);
        matched = true;
      }

      // Handle the Slurm license naming/enforcement arguments.
      if (!matched && key == "iqm_license_prefix") {
        license_prefix_ = std::string{value};
        matched = true;
      }

      if (!matched && key == "iqm_require_license") {
        require_license_ = parse_bool_arg(value, "iqm_require_license");
        matched = true;
      }

      if (!matched) {
        slurm_spank_log(
            "[iqm_spank_plugin] warning: unknown plugstack argument "
            "'%.*s'",
            static_cast<int>(key.size()), key.data());
      }
    }
  }

  /**
   * @brief Register srun command-line options for all IQM configuration keys.
   * @param spank SPANK handle (must be called from slurm_spank_init).
   * @return `ESPANK_SUCCESS` on success, `ESPANK_ERROR` on failure.
   */
  int register_options(spank_t spank) {
    for (std::size_t i = 0; i < K_CONFIG_MAPPINGS.size(); ++i) {
      const auto &mapping = K_CONFIG_MAPPINGS[i];
      // spank_option fields are non-const char* in the Slurm API.
      // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
      srun_option_defs_[i] = {
          .name = const_cast<char *>(mapping.option_name),
          .arginfo = const_cast<char *>("value"),
          .usage = const_cast<char *>(mapping.option_usage),
          .has_arg = 1,
          .val = static_cast<int>(i),
          .cb = &IQMSpankConfigManager::option_callback,
      };
      // NOLINTEND(cppcoreguidelines-pro-type-const-cast)

      const auto rc = spank_option_register(spank, &srun_option_defs_[i]);
      if (rc != ESPANK_SUCCESS) {
        slurm_spank_log(
            "[iqm_spank_plugin] error: failed to register option --%s: %s",
            mapping.option_name, spank_strerror(rc));
        return ESPANK_ERROR;
      }
    }
    return ESPANK_SUCCESS;
  }

  /**
   * @brief Inject configured values into the job environment.
   *
   * For each configuration key, the effective value is determined by
   * precedence:
   *   1. srun option (highest priority, overwrite=1)
   *   2. User-set environment variable (not overwritten)
   *   3. plugstack.conf default (overwrite=0)
   *
   * @param spank SPANK handle for the current hook invocation.
   * @return `ESPANK_SUCCESS` on success, `ESPANK_ERROR` if injection fails.
   */
  [[nodiscard]] int inject_environment(spank_t spank) const {
    for (std::size_t i = 0; i < K_CONFIG_MAPPINGS.size(); ++i) {
      // Determine effective value: srun option > plugstack default.
      const std::string *effective_value = nullptr;
      int overwrite = 0;

      if (srun_values_[i].has_value()) {
        // srun option: overwrite everything (highest precedence).
        effective_value = &srun_values_[i].value();
        overwrite = 1;
      } else if (plugstack_values_[i].has_value()) {
        // plugstack default: do not overwrite user env vars.
        effective_value = &plugstack_values_[i].value();
        overwrite = 0;
      }

      if (effective_value == nullptr) {
        continue;
      }

      const std::string env_name{K_CONFIG_MAPPINGS[i].env_var};
      const auto rc = spank_setenv(spank, env_name.c_str(),
                                   effective_value->c_str(), overwrite);
      if (rc != ESPANK_SUCCESS && rc != ESPANK_ENV_EXISTS) {
        slurm_spank_log("[iqm_spank_plugin] error: failed to set %s: %s",
                        env_name.c_str(), spank_strerror(rc));
        return ESPANK_ERROR;
      }

      if (rc == ESPANK_ENV_EXISTS) {
        slurm_debug(
            "[iqm_spank_plugin] %s already set by user, keeping user value",
            env_name.c_str());
      } else {
        const auto *source =
            srun_values_[i].has_value() ? "srun option" : "plugstack config";
        slurm_debug("[iqm_spank_plugin] set %s from %s", env_name.c_str(),
                    source);
      }
    }
    return ESPANK_SUCCESS;
  }
  /**
   * @brief Validate that the effective configuration is non-contradictory.
   *
   * Performs only superficial SPANK-side checks:
   *   1. `IQM_BASE_URL` must be set.
   *   2. `IQM_TOKEN` and `IQM_TOKENS_FILE` must not both be set.
   *
   * Deeper semantic/runtime validation is intentionally deferred to the
   * IQM QDMI device implementation.
   *
   * @param spank SPANK handle for the current hook invocation.
   * @return `ESPANK_SUCCESS` if valid, `ESPANK_ERROR` on conflict.
   */
  [[nodiscard]] static int validate_environment(spank_t spank) {
    const bool has_base_url = env_is_set(spank, "IQM_BASE_URL");
    const bool has_token = env_is_set(spank, "IQM_TOKEN");
    const bool has_tokens_file = env_is_set(spank, "IQM_TOKENS_FILE");

    if (!has_base_url) {
      slurm_spank_log(
          "[iqm_spank_plugin] error: missing required IQM_BASE_URL in job "
          "environment");
      return ESPANK_ERROR;
    }

    if (has_token && has_tokens_file) {
      slurm_spank_log(
          "[iqm_spank_plugin] error: conflicting auth — both "
          "IQM_TOKEN and IQM_TOKENS_FILE are set in job environment");
      return ESPANK_ERROR;
    }

    const auto *auth_source = "unset";
    if (has_token) {
      auth_source = "token";
    } else if (has_tokens_file) {
      auth_source = "tokens_file";
    }

    slurm_debug("[iqm_spank_plugin] validation status=ok base_url=%s "
                "auth_source=%s",
                has_base_url ? "set" : "unset", auth_source);

    return ESPANK_SUCCESS;
  }

  /**
   * @brief Validate that the job's requested Slurm license (native
   * `--licenses`/`-L`) aligns with the targeted QC alias.
   *
   * Note: "Slurm license" refers to Slurm's native countable-resource
   * mechanism (`Licenses=` in slurm.conf, `--licenses` at submission) — a
   * capacity-limiting scheduler resource, unrelated to GPLv3/software
   * licensing.
   *
   * The expected license name is `<iqm_license_prefix><alias>`, with `:`
   * and `,` in the alias replaced by `_` since Slurm's `name:count` syntax
   * reserves those characters (see `sanitize_alias_for_license()`). The
   * check is skipped (returns `ESPANK_SUCCESS`, debug log only) when:
   *   - No `IQM_QC_ALIAS` is resolved (`IQM_QC_ID`-only jobs are not
   *     checked; raw IDs are not reliably convertible into license names).
   *   - `SLURM_JOB_LICENSES` is absent — true both on Slurm < 23.02 (the
   *     variable does not exist there) and when the user requested no
   *     `--licenses` at all. These two cases cannot be distinguished from
   *     inside the plugin, so absence is always treated as a no-op. A
   *     value too long to fit the lookup buffer is treated as a mismatch
   *     instead (fail closed, not skipped).
   *
   * Note on enforcement: returning `ESPANK_ERROR` here fails the task at
   * launch in `slurm_spank_task_init` (remote context) — i.e. *after* the
   * job has already been submitted, queued, and allocated, not at
   * submission time — and only takes effect if the plugin is declared
   * `required` (not `optional`) in plugstack.conf.
   *
   * @param spank SPANK handle for the current hook invocation.
   * @return `ESPANK_SUCCESS` if valid or skipped, `ESPANK_ERROR` only when
   *   `iqm_require_license` is enabled and the expected license is missing.
   */
  [[nodiscard]] int validate_license_alignment(spank_t spank) const {
    const auto alias = get_spank_env(spank, "IQM_QC_ALIAS");
    if (!alias.has_value() || alias->empty()) {
      slurm_debug(
          "[iqm_spank_plugin] no IQM_QC_ALIAS resolved, skipping Slurm "
          "license alignment check (IQM_QC_ID-only jobs are not checked)");
      return ESPANK_SUCCESS;
    }

    const auto expected_license = expected_license_name(alias.value());

    // Read SLURM_JOB_LICENSES directly (rather than via get_spank_env())
    // so ESPANK_NOSPACE (value too long for the buffer) can be told apart
    // from a truly absent variable: silently skipping in the former case
    // would fail open under iqm_require_license.
    constexpr int k_licenses_buf_size = 4096;
    std::array<char, k_licenses_buf_size> licenses_buf{};
    const auto licenses_rc =
        spank_getenv(spank, "SLURM_JOB_LICENSES", licenses_buf.data(),
                     static_cast<int>(licenses_buf.size()));

    if (licenses_rc != ESPANK_SUCCESS && licenses_rc != ESPANK_NOSPACE) {
      slurm_debug(
          "[iqm_spank_plugin] SLURM_JOB_LICENSES not present in job "
          "environment (requires Slurm >=23.02, or no --licenses requested); "
          "skipping Slurm license alignment check");
      return ESPANK_SUCCESS;
    }

    bool aligned = false;
    if (licenses_rc == ESPANK_NOSPACE) {
      // The value doesn't fit the buffer; treat as unverifiable rather than
      // silently skipping the check (which would fail open under
      // iqm_require_license).
      slurm_debug(
          "[iqm_spank_plugin] SLURM_JOB_LICENSES exceeds %d bytes, cannot "
          "verify Slurm license alignment for QC alias '%s'; treating as a "
          "mismatch",
          k_licenses_buf_size, alias->c_str());
    } else {
      aligned = license_list_contains(std::string_view{licenses_buf.data()},
                                      expected_license);
    }

    if (aligned) {
      slurm_debug(
          "[iqm_spank_plugin] Slurm license alignment ok: '%s' requested for "
          "QC alias '%s'",
          expected_license.c_str(), alias->c_str());
      return ESPANK_SUCCESS;
    }

    if (!require_license_) {
      slurm_spank_log(
          "[iqm_spank_plugin] warning: Slurm license not requested for QC "
          "alias '%s' (expected --licenses=%s:<n>); on-premise QCs may "
          "experience uncontrolled concurrency without it since there is no "
          "cloud-side queue to absorb overlapping requests. Set "
          "iqm_require_license=1 to enforce this as a hard requirement",
          alias->c_str(), expected_license.c_str());
      return ESPANK_SUCCESS;
    }

    slurm_spank_log(
        "[iqm_spank_plugin] error: missing required Slurm license '%s' for "
        "QC alias '%s' — resubmit with --licenses=%s:<n> "
        "(iqm_require_license is enabled)",
        expected_license.c_str(), alias->c_str(), expected_license.c_str());
    return ESPANK_ERROR;
  }

  /**
   * @brief Check whether the plugin is active for the given partition.
   *
   * If no partition filter is configured, the plugin is active on all
   * partitions. Otherwise, the job's partition must appear in the configured
   * list.
   *
   * @param spank SPANK handle for the current hook invocation.
   * @return `true` if the plugin should proceed, `false` to skip.
   */
  [[nodiscard]] bool is_active_for_job(spank_t spank) const {
    if (active_partitions_.empty()) {
      return true; // No filter configured; active on all partitions.
    }

    const auto partition = get_spank_env(spank, "SLURM_JOB_PARTITION");
    if (!partition.has_value()) {
      slurm_debug("[iqm_spank_plugin] could not determine job partition, "
                  "proceeding anyway");
      return true;
    }

    for (const auto &p : active_partitions_) {
      if (partition.value() == p) {
        return true;
      }
    }

    slurm_debug(
        "[iqm_spank_plugin] partition '%s' not in active list, skipping",
        partition.value().c_str());
    return false;
  }

  /**
   * @brief Check accessibility of the IQM_TOKENS_FILE if configured.
   *
   * Emits a warning if the file does not exist or is not readable in the
   * task context. This runs on the compute node as the job user.
   *
   * @param spank SPANK handle.
   */
  static void check_tokens_file_access(spank_t spank) {
    const auto path = get_spank_env(spank, "IQM_TOKENS_FILE");
    if (!path.has_value()) {
      return;
    }

    const std::filesystem::path tokens_path{path.value()};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(tokens_path, ec) || ec) {
      slurm_spank_log("[iqm_spank_plugin] warning: IQM_TOKENS_FILE '%s' is not "
                      "readable or does not exist",
                      path.value().c_str());
    }
  }

  /**
   * @brief Emit a structured diagnostic summary line for the current job.
   *
   * Format:
   *   [iqm_spank_plugin] job=<id> partition=<name> base_url=<set|unset>
   *     auth=<token|tokens_file|unset> tokens_file_ok=<yes|no|n/a>
   *     license=<name>:<ok|missing|n/a>
   *
   * @param spank SPANK handle.
   */
  void emit_diagnostics(spank_t spank) const {
    const auto job_id = get_spank_env(spank, "SLURM_JOB_ID");
    const auto partition = get_spank_env(spank, "SLURM_JOB_PARTITION");

    const bool has_base_url = env_is_set(spank, "IQM_BASE_URL");
    const bool has_token = env_is_set(spank, "IQM_TOKEN");
    const bool has_tokens_file = env_is_set(spank, "IQM_TOKENS_FILE");

    const auto *auth_str = "unset";
    if (has_token) {
      auth_str = "token";
    } else if (has_tokens_file) {
      auth_str = "tokens_file";
    }

    // Tokens file accessibility.
    auto tf_ok = std::string{"n/a"};
    if (has_tokens_file) {
      if (const auto path = get_spank_env(spank, "IQM_TOKENS_FILE")) {
        std::error_code ec;
        tf_ok = std::filesystem::is_regular_file(path.value(), ec) && !ec
                    ? "yes"
                    : "no";
      }
    }

    // Slurm license alignment (see validate_license_alignment()).
    auto license_str = std::string{"none:n/a"};
    if (const auto alias = get_spank_env(spank, "IQM_QC_ALIAS");
        alias.has_value() && !alias->empty()) {
      const auto expected_license = expected_license_name(alias.value());
      if (const auto licenses_env = get_spank_env(spank, "SLURM_JOB_LICENSES");
          licenses_env.has_value()) {
        const bool found =
            license_list_contains(licenses_env.value(), expected_license);
        license_str = expected_license + ":" + (found ? "ok" : "missing");
      } else {
        license_str = expected_license + ":n/a";
      }
    }

    const auto msg = std::format(
        "[iqm_spank_plugin] job={} partition={} base_url={} auth={} "
        "tokens_file_ok={} license={}",
        job_id.value_or("unknown"), partition.value_or("unknown"),
        has_base_url ? "set" : "unset", auth_str, tf_ok, license_str);

    slurm_spank_log("%s", msg.c_str());
  }
  /**
   * @brief Log a summary of parsed plugstack configuration.
   */
  void log_parsed_config() const {
    const auto configured_count = static_cast<int>(
        std::count_if(plugstack_values_.cbegin(), plugstack_values_.cend(),
                      [](const auto &v) { return v.has_value(); }));
    slurm_debug("[iqm_spank_plugin] parsed %d plugstack argument(s), "
                "%zu active partition(s), require_license=%s",
                configured_count, active_partitions_.size(),
                require_license_ ? "true" : "false");
  }

private:
  /**
   * @brief Callback invoked by Slurm when a user provides an --iqm-* option.
   *
   * This is a static C-linkage callback required by the spank_option API.
   * The `val` parameter identifies which option was provided (index into
   * K_CONFIG_MAPPINGS). The value is stored in `g_config.srun_values_`.
   *
   * @param val Option identifier (index into K_CONFIG_MAPPINGS).
   * @param optarg User-provided argument string.
   * @param remote Whether this is called in remote context.
   * @return 0 on success, -1 on error.
   */
  static int option_callback(int val, const char *optarg, int remote);

  /**
   * @brief Parse a comma-separated partition list from plugstack.conf.
   * @param value Comma-separated partition names (e.g. "quantum,quantum-dev").
   */
  void parse_partition_list(std::string_view value) {
    active_partitions_.clear();
    while (!value.empty()) {
      const auto comma = value.find(',');
      auto token = value.substr(0, comma);
      // Trim leading/trailing whitespace.
      while (!token.empty() && token.front() == ' ') {
        token.remove_prefix(1);
      }
      while (!token.empty() && token.back() == ' ') {
        token.remove_suffix(1);
      }
      if (!token.empty()) {
        active_partitions_.emplace_back(token);
      }
      value = (comma == std::string_view::npos) ? "" : value.substr(comma + 1);
    }
  }

  /**
   * @brief Parse a boolean-ish plugstack argument value, case-insensitively.
   *
   * Recognizes `1`/`true`/`yes`/`on`/`enabled` as true and
   * `0`/`false`/`no`/`off`/`disabled` as false. Logs a warning and returns
   * `false` for any other value, so a misspelled value (e.g. `TRUE`, `on`,
   * or a typo) is never silently mistaken for `false` without a trace in
   * the logs.
   *
   * @param value The raw plugstack argument value.
   * @param arg_name The plugstack key name, used only for the warning message.
   * @return The parsed boolean value.
   */
  [[nodiscard]] static bool parse_bool_arg(std::string_view value,
                                           const char *arg_name) {
    std::string normalized{value};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on" || normalized == "enabled") {
      return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off" || normalized == "disabled") {
      return false;
    }
    slurm_spank_log(
        "[iqm_spank_plugin] warning: unrecognized value '%.*s' for %s, "
        "treating as false (expected one of: 1, true, yes, on, enabled / 0, "
        "false, no, off, disabled)",
        static_cast<int>(value.size()), value.data(), arg_name);
    return false;
  }

  /**
   * @brief Compute the expected Slurm license name for a QC alias.
   *
   * Replaces `:` and `,` in the alias with `_`, since Slurm's `name:count`
   * license syntax reserves those characters (aliases may legitimately
   * contain a colon, e.g. the `:mock` suffix used to select simulator
   * variants, as in `emerald:mock`).
   *
   * @param alias The resolved IQM_QC_ALIAS value.
   * @return `<license_prefix_><sanitized alias>`.
   */
  [[nodiscard]] std::string
  expected_license_name(std::string_view alias) const {
    return license_prefix_.value_or("iqm_qc_") +
           sanitize_alias_for_license(alias);
  }

  /**
   * @brief Replace `:` and `,` in a QC alias with `_` so it is safe to use
   * as a Slurm license name.
   * @param alias The QC alias to sanitize.
   * @return The sanitized alias.
   */
  [[nodiscard]] static std::string
  sanitize_alias_for_license(std::string_view alias) {
    std::string sanitized{alias};
    std::ranges::replace(sanitized, ':', '_');
    std::ranges::replace(sanitized, ',', '_');
    return sanitized;
  }

  /**
   * @brief Check whether a Slurm license list (as found in
   * `SLURM_JOB_LICENSES`, e.g. "name:count,name2:count2") contains a given
   * license name.
   *
   * Each comma-separated token is cut at its first `:` (count separator),
   * `*` (legacy count separator on some Slurm versions), or `@` (remote/
   * federated license server qualifier) before comparison, so qualified
   * tokens like `name@server:count` still match a bare `name`.
   *
   * @param licenses_value The raw SLURM_JOB_LICENSES value.
   * @param license_name The license name to look for.
   * @return `true` if `license_name` appears in `licenses_value`.
   */
  [[nodiscard]] static bool
  license_list_contains(std::string_view licenses_value,
                        std::string_view license_name) {
    while (!licenses_value.empty()) {
      const auto comma = licenses_value.find(',');
      auto token = licenses_value.substr(0, comma);
      if (const auto sep = token.find_first_of(":*@");
          sep != std::string_view::npos) {
        token = token.substr(0, sep);
      }
      while (!token.empty() && token.front() == ' ') {
        token.remove_prefix(1);
      }
      while (!token.empty() && token.back() == ' ') {
        token.remove_suffix(1);
      }
      if (token == license_name) {
        return true;
      }
      licenses_value = (comma == std::string_view::npos)
                           ? ""
                           : licenses_value.substr(comma + 1);
    }
    return false;
  }

  /**
   * @brief Retrieve a SPANK environment variable as a std::string.
   * @param spank SPANK handle.
   * @param var Variable name.
   * @return The variable value, or std::nullopt if not set or unavailable.
   */
  static std::optional<std::string> get_spank_env(spank_t spank,
                                                  const char *var) {
    constexpr int k_buf_size = 4096;
    std::array<char, k_buf_size> buf{};
    const auto rc = spank_getenv(spank, var, buf.data(), buf.size());
    if (rc == ESPANK_SUCCESS && buf[0] != '\0') {
      return std::string{buf.data()};
    }
    return std::nullopt;
  }

  /**
   * @brief Check whether an environment variable is set in the job
   * environment.
   * @param spank SPANK handle.
   * @param var Variable name.
   * @return `true` if the variable exists and is non-empty.
   */
  static bool env_is_set(spank_t spank, const char *var) {
    // Try spank_getenv (remote context). Fall back to getenv (local context).
    std::array<char, 2> buf{};
    const auto rc =
        spank_getenv(spank, var, buf.data(), static_cast<int>(buf.size()));
    if (rc == ESPANK_SUCCESS) {
      return buf[0] != '\0';
    }
    if (rc == ESPANK_NOSPACE) {
      // Variable exists and is longer than our buffer; that's fine.
      return true;
    }
    // Fallback to local context where spank_getenv is not available.
    if (rc == ESPANK_NOT_REMOTE) {
      if (const auto *val = std::getenv(var);
          val != nullptr && val[0] != '\0') {
        return true;
      }
    }
    return false;
  }

  /// Stored plugstack-configured values, indexed in parallel with
  /// K_CONFIG_MAPPINGS.
  std::array<std::optional<std::string>, K_CONFIG_MAPPINGS.size()>
      plugstack_values_{};

  /// Values provided via srun command-line options (highest precedence).
  std::array<std::optional<std::string>, K_CONFIG_MAPPINGS.size()>
      srun_values_{};

  /// Registered spank_option structs (must remain valid for Slurm's lifetime).
  std::array<spank_option, K_CONFIG_MAPPINGS.size()> srun_option_defs_{};

  /// Partition filter: when non-empty, the plugin is only active on jobs
  /// targeting one of these partitions.
  std::vector<std::string> active_partitions_;

  /// Prefix used to derive the expected Slurm license name from
  /// IQM_QC_ALIAS. Configurable via the `iqm_license_prefix` plugstack
  /// argument (default: "iqm_qc_").
  std::optional<std::string> license_prefix_;

  /// When true, jobs that resolve an IQM_QC_ALIAS but do not request the
  /// corresponding Slurm license via --licenses are rejected outright
  /// instead of only receiving a warning. Configurable via
  /// `iqm_require_license` (default: false/off).
  bool require_license_ = false;
};

/// Global plugin configuration instance. Populated once during SPANK init,
/// then read-only for subsequent hook invocations.
IQMSpankConfigManager g_config{}; // NOLINT(*-avoid-non-const-global-variables)

// Static callback implementation — stores the srun option value.
int IQMSpankConfigManager::option_callback(const int val, const char *optarg,
                                           int /* remote */) {
  if (val < 0 || static_cast<std::size_t>(val) >= K_CONFIG_MAPPINGS.size()) {
    slurm_spank_log("[iqm_spank_plugin] error: invalid option id %d", val);
    return -1;
  }
  if (optarg == nullptr || optarg[0] == '\0') {
    slurm_spank_log(
        "[iqm_spank_plugin] error: empty value for --%s",
        K_CONFIG_MAPPINGS[static_cast<std::size_t>(val)].option_name);
    return -1;
  }
  g_config.srun_values_[static_cast<std::size_t>(val)] = std::string{optarg};
  slurm_debug("[iqm_spank_plugin] srun option --%s provided",
              K_CONFIG_MAPPINGS[static_cast<std::size_t>(val)].option_name);
  return 0;
}

/**
 * @brief Emit a standardized hook marker to Slurm output for validation.
 * @param hook_name SPANK hook function name.
 */
void Emit_hook_log(const char *hook_name) {
  slurm_spank_log("[iqm_spank_plugin] hook=%s", hook_name);
}

} // namespace

// NOLINTBEGIN(readability-identifier-naming)

extern "C" {
/// Spank plugin name. Must be unique across all loaded plugins.
extern const char plugin_name[] = "iqm_spank_plugin";
/// Plugin type. Must be "spank" for SPANK plugins.
extern const char plugin_type[] = "spank";
/// Plugin version. Must match the Slurm version number for compatibility.
extern const unsigned int plugin_version = SLURM_VERSION_NUMBER;
/// Version number of the plugin.
extern const unsigned int spank_plugin_version = 1;

/**
 * @brief SPANK init hook.
 *
 * Parses plugstack.conf key=value arguments, stores them for later environment
 * injection, and registers srun command-line options.
 *
 * @return `ESPANK_SUCCESS` on success, otherwise `ESPANK_ERROR`.
 */
int slurm_spank_init(spank_t spank, const int ac, char **av) {
  Emit_hook_log("slurm_spank_init");
  g_config.parse_plugstack_args(ac, av);
  g_config.log_parsed_config();
  return g_config.register_options(spank);
}

/**
 * @brief SPANK post-option-init hook.
 * @return `ESPANK_SUCCESS`.
 */
int slurm_spank_init_post_opt(spank_t /* spank */, const int /* ac */,
                              char ** /* av */) {
  Emit_hook_log("slurm_spank_init_post_opt");
  return ESPANK_SUCCESS;
}

/**
 * @brief SPANK task init hook (unprivileged context).
 *
 * Injects IQM environment variables into the job environment and performs
 * validation of the resulting configuration.
 *
 * Slurm-ABI note: slurmstepd (`task.c`, `spank_user_task(...) < 0`) only
 * treats a *negative* return from this specific hook as fatal — it does
 * NOT check for nonzero. `ESPANK_ERROR` is `+3000` (see
 * `slurm/slurm_errno.h`), so returning it directly here would only log
 * "task_init() failed with rc=3000" and let the task launch anyway. This
 * wrapper therefore translates any internal validation failure to a plain
 * `-1` before returning, so a `required` plugin actually blocks the task.
 *
 * @return `ESPANK_SUCCESS` on success, `-1` if validation failed.
 */
int slurm_spank_task_init(spank_t spank, const int /* ac */, char ** /* av */) {
  Emit_hook_log("slurm_spank_task_init");

  // Partition filter: skip if job is not on an active partition.
  if (!g_config.is_active_for_job(spank)) {
    return ESPANK_SUCCESS;
  }

  if (const auto rc = g_config.inject_environment(spank);
      rc != ESPANK_SUCCESS) {
    return -1;
  }

  const auto validation_rc = IQMSpankConfigManager::validate_environment(spank);
  const auto license_validation_rc = g_config.validate_license_alignment(spank);

  // Auth file accessibility check (warning only).
  IQMSpankConfigManager::check_tokens_file_access(spank);

  // Structured diagnostic summary.
  g_config.emit_diagnostics(spank);

  if (validation_rc != ESPANK_SUCCESS ||
      license_validation_rc != ESPANK_SUCCESS) {
    return -1;
  }
  return ESPANK_SUCCESS;
}

/**
 * @brief SPANK task init hook (privileged context).
 * @return `ESPANK_SUCCESS`.
 */
int slurm_spank_task_init_privileged(spank_t /* spank */, const int /* ac */,
                                     char ** /* av */) {
  Emit_hook_log("slurm_spank_task_init_privileged");
  return ESPANK_SUCCESS;
}

/**
 * @brief SPANK exit hook.
 * @return `ESPANK_SUCCESS`.
 */
int slurm_spank_exit(spank_t /* spank */, const int /* ac */,
                     char ** /* av */) {
  Emit_hook_log("slurm_spank_exit");
  return ESPANK_SUCCESS;
}
}

// NOLINTEND(readability-identifier-naming)

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
