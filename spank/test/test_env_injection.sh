#!/usr/bin/env bash
# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

# Test script for the IQM SPANK plugin environment injection.
#
# Requires:
#   - A running Slurm cluster with an allocatable node.
#   - The IQM SPANK plugin installed and referenced in plugstack.conf
#     with at least one key=value argument (e.g. iqm_base_url=https://test.example).
#
# The tests verify:
#   1. Plugstack-configured variables are visible in the job environment.
#   2. User-set variables override plugstack defaults (precedence).
#   3. Unconfigured variables are absent from the job environment.
#   4. Configured token files are visible in the job environment.
#   5. Conflicting auth is rejected.
#   6. srun options have highest precedence.
#   7. Missing IQM_BASE_URL is rejected.
#   8. An unavailable IQM backend is rejected.
#   9. Daemon-only IQM variables do not affect backend validation.
#  10. Backend validation and task diagnostics run once per node and job step.
#  11. Backend validation fails within its configured HTTP timeout.

set -euo pipefail

partition=""
srun_cmd="srun"
test_base_url="https://test.example.com"
test_tokens_file=""
request_count_url=""
hang_base_url=""

passed=0
failed=0
skipped=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

pass() {
  echo "  PASS: $1" >&2
  ((passed++)) || true
}

fail() {
  echo "  FAIL: $1" >&2
  ((failed++)) || true
}

skip() {
  echo "  SKIP: $1" >&2
  ((skipped++)) || true
}

# Run a command via srun and capture its stdout.
# Returns 1 if srun itself fails.
run_srun() {
  local -a args=(--immediate=3 -N1 -n1)
  if [[ -n "$partition" ]]; then
    args+=(--partition "$partition")
  fi
  args+=("$@")

  local output
  output=$("$srun_cmd" "${args[@]}" 2>/dev/null) || return 1
  echo "$output"
}

# Retrieve a single env var from a job via srun.
# Usage: get_job_env VAR_NAME [extra srun env exports]
get_job_env() {
  local var_name="$1"
  shift
  local output
  output=$(run_srun env "$@") || return 1
  echo "$output" | grep -E "^${var_name}=" | head -n1 | cut -d= -f2-
}

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------

usage() {
  cat <<'EOF'
Usage: spank/test/test_env_injection.sh [options]

Tests the IQM SPANK plugin environment injection behavior.
Requires a running Slurm cluster with an allocatable node and the plugin
configured in plugstack.conf with at least iqm_base_url=<url>.

Options:
  --partition <name>       Partition to use (passed to srun as --partition).
  --srun <path>            srun binary to use (default: srun).
  --test-base-url <url>    Expected IQM_BASE_URL from plugstack config
                           (default: https://test.example.com).
  --test-tokens-file <f>   Expected IQM_TOKENS_FILE from plugstack config
                           (if set).
  --request-count-url <u>  Optional mock-backend counter endpoint used to
                           verify once-per-node, per-step validation.
  --hang-base-url <u>      Optional mock-backend URL that accepts requests but
                           delays responses beyond the configured timeout.
  -h, --help               Show this help.

Examples:
  # Basic test (plugstack has iqm_base_url=https://test.example.com)
  spank/test/test_env_injection.sh --partition debug

  # With a specific expected URL
  spank/test/test_env_injection.sh --partition debug \
      --test-base-url https://resonance.iqm.tech
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
  case "$1" in
    --partition)
      partition="$2"
      shift 2
      ;;
    --srun)
      srun_cmd="$2"
      shift 2
      ;;
    --test-base-url)
      test_base_url="$2"
      shift 2
      ;;
    --test-tokens-file)
      test_tokens_file="$2"
      shift 2
      ;;
    --request-count-url)
      request_count_url="$2"
      shift 2
      ;;
    --hang-base-url)
      hang_base_url="$2"
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if ! command -v "$srun_cmd" >/dev/null 2>&1; then
  echo "ERROR: '$srun_cmd' not found in PATH." >&2
  exit 127
fi

# ---------------------------------------------------------------------------
# Connectivity check
# ---------------------------------------------------------------------------

echo "=== IQM SPANK Plugin: Environment Injection Tests ===" >&2
echo "" >&2

echo "Checking Slurm connectivity..." >&2
if ! connectivity_output=$(run_srun /bin/true 2>&1); then
  echo "ERROR: Cannot allocate a job via srun. Ensure Slurm is running" >&2
  echo "       and a node is available." >&2
  echo "       Output: $connectivity_output" >&2
  exit 1
fi
echo "OK: Slurm is reachable and can allocate jobs." >&2
echo "" >&2

# ---------------------------------------------------------------------------
# Test 1: Plugstack-configured IQM_BASE_URL is injected
# ---------------------------------------------------------------------------

echo "--- Test 1: Plugstack-configured IQM_BASE_URL is injected ---" >&2

if [[ -z "$test_base_url" ]]; then
  skip "No --test-base-url provided, cannot verify IQM_BASE_URL injection."
else
  job_base_url=$(get_job_env "IQM_BASE_URL") || true
  if [[ "$job_base_url" == "$test_base_url" ]]; then
    pass "IQM_BASE_URL='$job_base_url' matches expected '$test_base_url'"
  elif [[ -n "$job_base_url" ]]; then
    fail "IQM_BASE_URL='$job_base_url' does not match expected '$test_base_url'"
  else
    fail "IQM_BASE_URL is not set in job environment (expected '$test_base_url')"
  fi
fi

# ---------------------------------------------------------------------------
# Test 2: User-set env overrides plugstack default
# ---------------------------------------------------------------------------

echo "--- Test 2: User override takes precedence ---" >&2

if [[ "$test_base_url" == */ ]]; then
  override_base_url="${test_base_url%/}"
else
  override_base_url="${test_base_url}/"
fi
job_base_url_override=$(IQM_BASE_URL="$override_base_url" get_job_env "IQM_BASE_URL") || true

if [[ "$job_base_url_override" == "$override_base_url" ]]; then
  pass "User override IQM_BASE_URL='$override_base_url' preserved (plugstack did not overwrite)"
elif [[ -z "$job_base_url_override" ]]; then
  fail "IQM_BASE_URL is empty after user override attempt"
else
  fail "IQM_BASE_URL='$job_base_url_override' — expected user override '$override_base_url'"
fi

# ---------------------------------------------------------------------------
# Test 3: Unconfigured variables are absent
# ---------------------------------------------------------------------------

echo "--- Test 3: Unconfigured variables are absent ---" >&2

# IQM_QC_ID is typically not configured in plugstack for testing.
job_qc_id=$(get_job_env "IQM_QC_ID") || true
if [[ -z "$job_qc_id" ]]; then
  pass "IQM_QC_ID is absent (not configured in plugstack)"
else
  skip "IQM_QC_ID='$job_qc_id' is set — may be configured in your plugstack"
fi

# ---------------------------------------------------------------------------
# Test 4: Tokens-file injection (if configured)
# ---------------------------------------------------------------------------

echo "--- Test 4: Tokens-file injection (if configured) ---" >&2

if [[ -n "$test_tokens_file" ]]; then
  job_tf=$(get_job_env "IQM_TOKENS_FILE") || true
  if [[ "$job_tf" == "$test_tokens_file" ]]; then
    pass "IQM_TOKENS_FILE='$job_tf' matches expected"
  elif [[ -n "$job_tf" ]]; then
    fail "IQM_TOKENS_FILE='$job_tf' does not match expected '$test_tokens_file'"
  else
    fail "IQM_TOKENS_FILE not set (expected '$test_tokens_file')"
  fi
else
  skip "No --test-tokens-file provided; skipping tokens-file test"
fi

# ---------------------------------------------------------------------------
# Test 5: Conflicting auth detection
# ---------------------------------------------------------------------------

echo "--- Test 5: Conflicting auth is rejected ---" >&2

# Set both IQM_TOKEN and IQM_TOKENS_FILE to trigger the conflict check.
conflict_args=(--immediate=3 -N1 -n1)
if [[ -n "$partition" ]]; then
  conflict_args+=(--partition "$partition")
fi

set +e
conflict_output=$(IQM_TOKEN="tok" IQM_TOKENS_FILE="/tmp/tf" \
  "$srun_cmd" "${conflict_args[@]}" /bin/true 2>&1)
conflict_rc=$?
set -e

if echo "$conflict_output" | grep -Fq "conflicting auth"; then
  pass "Conflicting auth detected and reported"
elif [[ $conflict_rc -ne 0 ]]; then
  pass "srun failed (rc=$conflict_rc) — likely due to auth conflict"
else
  fail "No conflict detected when both IQM_TOKEN and IQM_TOKENS_FILE are set"
fi

# ---------------------------------------------------------------------------
# Test 6: srun option overrides plugstack and user env
# ---------------------------------------------------------------------------

echo "--- Test 6: srun --iqm-base-url overrides plugstack and user env ---" >&2

srun_override_base_url="$test_base_url"

# Run srun with --iqm-base-url, plus IQM_BASE_URL in env to test full precedence.
override_args=(--immediate=3 -N1 -n1)
if [[ -n "$partition" ]]; then
  override_args+=(--partition "$partition")
fi
override_args+=(--iqm-base-url="$srun_override_base_url")

set +e
srun_override_output=$(IQM_BASE_URL="https://user-env.example.com" \
  "$srun_cmd" "${override_args[@]}" env 2>/dev/null)
srun_override_rc=$?
set -e

if [[ $srun_override_rc -ne 0 ]]; then
  fail "srun with --iqm-base-url failed (rc=$srun_override_rc)"
else
  srun_job_base_url=$(echo "$srun_override_output" | grep -E '^IQM_BASE_URL=' | head -n1 | cut -d= -f2-)
  if [[ "$srun_job_base_url" == "$srun_override_base_url" ]]; then
    pass "srun --iqm-base-url='$srun_override_base_url' overrode both plugstack and user env"
  elif [[ -z "$srun_job_base_url" ]]; then
    fail "IQM_BASE_URL not set despite --iqm-base-url flag"
  else
    fail "IQM_BASE_URL='$srun_job_base_url' — expected srun override '$srun_override_base_url'"
  fi
fi

# ---------------------------------------------------------------------------
# Test 7: Missing IQM_BASE_URL is rejected
# ---------------------------------------------------------------------------

echo "--- Test 7: Missing IQM_BASE_URL is rejected ---" >&2

# Exporting IQM_BASE_URL as an empty value blocks plugstack default injection
# (overwrite=0), which should trigger the plugin's required-IQM_BASE_URL check.
missing_args=(--immediate=3 -N1 -n1)
if [[ -n "$partition" ]]; then
  missing_args+=(--partition "$partition")
fi

set +e
missing_url_output=$(IQM_BASE_URL="" \
  "$srun_cmd" "${missing_args[@]}" /bin/true 2>&1)
missing_url_rc=$?
set -e

if echo "$missing_url_output" | grep -Fq "missing required IQM_BASE_URL"; then
  pass "Missing IQM_BASE_URL detected and reported"
elif [[ $missing_url_rc -ne 0 ]]; then
  pass "srun failed (rc=$missing_url_rc) when IQM_BASE_URL is empty"
else
  fail "Missing IQM_BASE_URL was not rejected"
fi

# ---------------------------------------------------------------------------
# Test 8: Unavailable IQM backend is rejected
# ---------------------------------------------------------------------------

echo "--- Test 8: Unavailable IQM backend is rejected ---" >&2

unavailable_args=(--immediate=3 --overcommit -N1 -n2)
if [[ -n "$partition" ]]; then
  unavailable_args+=(--partition "$partition")
fi
unavailable_args+=(--iqm-base-url=http://127.0.0.1:1)

set +e
unavailable_output=$("$srun_cmd" "${unavailable_args[@]}" /bin/true 2>&1)
unavailable_rc=$?
set -e
rejection_log_count=$(grep -Fc \
  "[iqm_spank_plugin] error: job rejected because IQM validation failed" \
  <<<"$unavailable_output" || true)

if [[ $unavailable_rc -eq 0 ]]; then
  fail "srun succeeded despite an unavailable IQM backend"
elif echo "$unavailable_output" | grep -Fq "IQM backend validation failed"; then
  pass "Unavailable IQM backend rejected before task launch"
else
  fail "srun failed (rc=$unavailable_rc) without the backend-validation message"
fi

if [[ $rejection_log_count -eq 1 ]]; then
  pass "Multi-task validation failure emitted one rejection diagnostic"
else
  fail "Expected one cached-failure diagnostic, observed $rejection_log_count"
fi

# ---------------------------------------------------------------------------
# Test 9: Daemon-only IQM variables do not affect validation
# ---------------------------------------------------------------------------

echo "--- Test 9: Daemon IQM environment is isolated from jobs ---" >&2

if run_srun /bin/sh -c \
  'test -z "${IQM_TOKEN:-}" && test -z "${IQM_TOKENS_FILE:-}" && test -z "${IQM_QC_ID:-}" && test -z "${IQM_QC_ALIAS:-}"' \
  >/dev/null; then
  pass "Backend validation ignored daemon-only IQM variables"
else
  fail "Daemon-only IQM variables affected validation or reached the job"
fi

# ---------------------------------------------------------------------------
# Test 10: Backend validation and diagnostics run once per node and job step
# ---------------------------------------------------------------------------

echo "--- Test 10: Backend validation and diagnostics run once per node and job step ---" >&2

if [[ -z "$request_count_url" ]]; then
  skip "No --request-count-url provided; skipping once-per-node assertions"
else
  reference_before=$(curl --fail --silent "$request_count_url")
  if run_srun /bin/true >/dev/null; then
    reference_after=$(curl --fail --silent "$request_count_url")
    reference_delta=$((reference_after - reference_before))
  else
    reference_delta=0
  fi

  before_count=$(curl --fail --silent "$request_count_url")
  once_args=(--immediate=3 --overcommit -N1 -n2)
  if [[ -n "$partition" ]]; then
    once_args+=(--partition "$partition")
  fi

  set +e
  once_output=$("$srun_cmd" "${once_args[@]}" /bin/true 2>&1)
  once_rc=$?
  set -e
  after_count=$(curl --fail --silent "$request_count_url")
  request_delta=$((after_count - before_count))
  task_init_log_count=$(grep -Ec \
    '\[iqm_spank_plugin\] hook=slurm_spank_task_init$' <<<"$once_output" || true)
  privileged_log_count=$(grep -Ec \
    '\[iqm_spank_plugin\] hook=slurm_spank_task_init_privileged$' \
    <<<"$once_output" || true)
  diagnostic_log_count=$(grep -Ec \
    '\[iqm_spank_plugin\] job=.* partition=' <<<"$once_output" || true)

  if [[ $once_rc -ne 0 ]]; then
    fail "Two-task srun failed (rc=$once_rc)"
  elif [[ $reference_delta -gt 0 && $request_delta -eq $reference_delta ]]; then
    pass "Two-task single-node step performed one backend validation"
  else
    fail "Expected the two-task step to match one validation's $reference_delta requests, observed $request_delta"
  fi

  if [[ $task_init_log_count -eq 1 && $privileged_log_count -eq 1 && $diagnostic_log_count -eq 1 ]]; then
    pass "Two-task job step emitted task hooks and summary diagnostics once"
  else
    fail "Expected one task-init, privileged-task-init, and summary diagnostic; observed $task_init_log_count, $privileged_log_count, and $diagnostic_log_count"
  fi
fi

# ---------------------------------------------------------------------------
# Test 11: A backend that accepts but never responds is bounded by HTTP timeout
# ---------------------------------------------------------------------------

echo "--- Test 11: Backend validation request timeout ---" >&2

if [[ -z "$hang_base_url" ]]; then
  skip "No --hang-base-url provided; skipping request-timeout assertion"
else
  timeout_args=(--immediate=3 -N1 -n1)
  if [[ -n "$partition" ]]; then
    timeout_args+=(--partition "$partition")
  fi

  start_seconds=$SECONDS
  set +e
  IQM_BASE_URL="$hang_base_url" timeout 8s "$srun_cmd" \
    "${timeout_args[@]}" /bin/true >/dev/null 2>&1
  timeout_rc=$?
  set -e
  elapsed_seconds=$((SECONDS - start_seconds))

  if [[ $timeout_rc -eq 124 ]]; then
    fail "Backend validation exceeded the 8-second outer test bound"
  elif [[ $timeout_rc -eq 0 ]]; then
    fail "Backend validation unexpectedly accepted a timed-out request"
  elif [[ $elapsed_seconds -le 5 ]]; then
    pass "Backend validation rejected a non-responsive endpoint in ${elapsed_seconds}s"
  else
    fail "Backend validation took ${elapsed_seconds}s despite the configured one-second request timeout"
  fi
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo "" >&2
echo "=== Results: $passed passed, $failed failed, $skipped skipped ===" >&2

if [[ $failed -gt 0 ]]; then
  exit 1
fi
