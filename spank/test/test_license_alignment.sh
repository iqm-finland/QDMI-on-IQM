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

# Test script for the IQM SPANK plugin's Slurm license alignment check.
#
# Requires:
#   - A running Slurm cluster with an allocatable node.
#   - The IQM SPANK plugin installed and referenced in plugstack.conf
#     with at least iqm_base_url=<url> (iqm_require_license off/unset).
#   - A `Licenses=` pool in slurm.conf defining iqm_qc_<alias>,
#     iqm_qc_<alias>_mock, and a distinct license name (see --test-other-license)
#     — see spank/config/slurm.conf for the Docker test fixture.
#
# The tests verify:
#   1. A mismatched --licenses request logs a warning but does not fail the job.
#   2. A matching --licenses request is silently accepted.
#   3. Aliases containing ':' (e.g. "emerald:mock") are sanitized to '_' when
#      deriving the expected license name.
#   4. No --licenses request at all is a silent no-op.
#   5. With iqm_require_license=1, a mismatched request is rejected outright.

set -euo pipefail

partition=""
srun_cmd="srun"
test_qc_alias="emerald"
test_other_license="other"
plugstack_conf="/etc/slurm/plugstack.conf.d/iqm-qdmi.conf"

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

# Run a command via srun and capture combined stdout+stderr, without
# aborting the script on a non-zero exit (the caller inspects $rc).
run_srun_capture() {
  local -a args=(--immediate=3 -N1 -n1)
  if [[ -n "$partition" ]]; then
    args+=(--partition "$partition")
  fi
  args+=("$@")

  set +e
  output=$("$srun_cmd" "${args[@]}" 2>&1)
  rc=$?
  set -e
}

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------

usage() {
  cat <<'EOF'
Usage: spank/test/test_license_alignment.sh [options]

Tests the IQM SPANK plugin's Slurm license alignment check (see
validate_license_alignment() in iqm_spank_plugin.cpp). Requires a running
Slurm cluster with an allocatable node, the plugin configured in
plugstack.conf with at least iqm_base_url=<url>, and a Licenses= pool
defining iqm_qc_<alias>, iqm_qc_<alias>_mock, and a distinct license name.

Options:
  --partition <name>          Partition to use (passed to srun as --partition).
  --srun <path>                srun binary to use (default: srun).
  --test-qc-alias <alias>      QC alias to test with (default: emerald).
  --test-other-license <name>  A distinct, pool-defined license name to use
                                for mismatch tests (default: other).
  -h, --help                   Show this help.
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
    --test-qc-alias)
      test_qc_alias="$2"
      shift 2
      ;;
    --test-other-license)
      test_other_license="$2"
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

expected_license="iqm_qc_${test_qc_alias}"
expected_mock_license="iqm_qc_${test_qc_alias}_mock"

# ---------------------------------------------------------------------------
# Connectivity check
# ---------------------------------------------------------------------------

echo "=== IQM SPANK Plugin: Slurm License Alignment Tests ===" >&2
echo "" >&2

echo "Checking Slurm connectivity..." >&2
connectivity_args=(--immediate=3 -N1 -n1)
if [[ -n "$partition" ]]; then
  connectivity_args+=(--partition "$partition")
fi
if ! "$srun_cmd" "${connectivity_args[@]}" /bin/true >/dev/null 2>&1; then
  echo "ERROR: Cannot allocate a job via srun. Ensure Slurm is running" >&2
  echo "       and a node is available." >&2
  exit 1
fi
echo "OK: Slurm is reachable and can allocate jobs." >&2
echo "" >&2

# ---------------------------------------------------------------------------
# Test 1: Mismatched --licenses logs a warning but does not fail the job
# ---------------------------------------------------------------------------

echo "--- Test 1: Mismatched Slurm license logs a warning ---" >&2

run_srun_capture --iqm-qc-alias="$test_qc_alias" "--licenses=${test_other_license}:1" env
if [[ $rc -ne 0 ]]; then
  fail "srun failed (rc=$rc) for a mismatched-license job (should only warn)"
elif echo "$output" | grep -Fq "Slurm license not requested"; then
  pass "Mismatched Slurm license detected and reported as a warning"
else
  fail "No warning reported for a mismatched Slurm license request"
fi

# ---------------------------------------------------------------------------
# Test 2: Matching --licenses request is silently accepted
# ---------------------------------------------------------------------------

echo "--- Test 2: Matching Slurm license is silently accepted ---" >&2

run_srun_capture --iqm-qc-alias="$test_qc_alias" "--licenses=${expected_license}:1" env
if [[ $rc -ne 0 ]]; then
  fail "srun failed (rc=$rc) for an aligned-license job"
elif echo "$output" | grep -Fq "Slurm license not requested"; then
  fail "Unexpected warning for a matching Slurm license request"
else
  pass "Matching Slurm license accepted without warning"
fi

# ---------------------------------------------------------------------------
# Test 3: Aliases containing ':' are sanitized to '_'
# ---------------------------------------------------------------------------

echo "--- Test 3: Alias ':' is sanitized to '_' in the expected license name ---" >&2

run_srun_capture --iqm-qc-alias="${test_qc_alias}:mock" "--licenses=${expected_mock_license}:1" env
if [[ $rc -ne 0 ]]; then
  fail "srun failed (rc=$rc) for a sanitized-alias aligned-license job"
elif echo "$output" | grep -Fq "Slurm license not requested"; then
  fail "Unexpected warning — alias ':' sanitization does not match expected '${expected_mock_license}'"
else
  pass "Sanitized alias '${test_qc_alias}:mock' matched license '${expected_mock_license}'"
fi

# ---------------------------------------------------------------------------
# Test 4: No --licenses request at all is a silent no-op
# ---------------------------------------------------------------------------

echo "--- Test 4: No --licenses request is a silent no-op ---" >&2

run_srun_capture --iqm-qc-alias="$test_qc_alias" env
if [[ $rc -ne 0 ]]; then
  fail "srun failed (rc=$rc) for a job without any --licenses request"
elif echo "$output" | grep -Fq "Slurm license not requested"; then
  fail "Unexpected warning when no --licenses was requested at all"
else
  pass "No warning when no Slurm license was requested (SLURM_JOB_LICENSES absent)"
fi

# ---------------------------------------------------------------------------
# Test 5: iqm_require_license=1 rejects a mismatched request
# ---------------------------------------------------------------------------

echo "--- Test 5: iqm_require_license=1 rejects a mismatched request ---" >&2

if [[ ! -f "$plugstack_conf" ]] || ! sudo test -w "$(dirname "$plugstack_conf")" 2>/dev/null; then
  skip "Cannot locate/write $plugstack_conf; skipping hard-reject scenario (likely running against a real external cluster rather than the Docker test fixture)"
else
  original_conf=$(sudo cat "$plugstack_conf")

  restore_conf() {
    echo "$original_conf" | sudo tee "$plugstack_conf" >/dev/null
    sudo scontrol reconfigure >/dev/null 2>&1 || true
  }
  trap restore_conf EXIT

  {
    echo "$original_conf"
    echo "    iqm_require_license=1"
  } | sudo tee "$plugstack_conf" >/dev/null
  sudo scontrol reconfigure >/dev/null 2>&1 || true
  sleep 1

  run_srun_capture --iqm-qc-alias="$test_qc_alias" "--licenses=${test_other_license}:1" env
  if [[ $rc -eq 0 ]]; then
    fail "srun succeeded (rc=0) despite iqm_require_license=1 and a mismatched license"
  elif echo "$output" | grep -Fq "missing required Slurm license"; then
    pass "Mismatched Slurm license rejected with iqm_require_license=1 (rc=$rc)"
  else
    fail "srun failed (rc=$rc) but not with the expected 'missing required Slurm license' message"
  fi

  restore_conf
  trap - EXIT
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo "" >&2
echo "=== Results: $passed passed, $failed failed, $skipped skipped ===" >&2

if [[ $failed -gt 0 ]]; then
  exit 1
fi
