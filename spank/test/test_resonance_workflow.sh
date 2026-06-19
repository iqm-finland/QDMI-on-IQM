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

# Test script to execute a real workflow on Resonance via Slurm using SPANK plugin.
#
# Requires:
#   - A running Slurm cluster with an allocatable node.
#   - The IQM SPANK plugin installed and referenced in plugstack.conf.
#   - IQM_TOKEN or IQM_TOKENS_FILE configured/available.

set -euo pipefail

partition=""
srun_cmd="srun"
test_base_url="https://resonance.iqm.tech"
test_qc_alias="emerald:mock"
test_tokens_file=""

usage() {
  cat <<'EOF'
Usage: spank/test/test_resonance_workflow.sh [options]

Runs a real workflow on Resonance via srun and the SPANK plugin.

Options:
  --partition <name>       Partition to use (passed to srun as --partition).
  --srun <path>            srun binary to use (default: srun).
  --test-base-url <url>    Expected IQM_BASE_URL (default: https://resonance.iqm.tech).
  --test-qc-alias <alias>  Expected IQM_QC_ALIAS (default: emerald:mock).
  --test-tokens-file <f>   Expected IQM_TOKENS_FILE path.
  -h, --help               Show this help.
EOF
}

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
    --test-qc-alias)
      test_qc_alias="$2"
      shift 2
      ;;
    --test-tokens-file)
      test_tokens_file="$2"
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

# Verify authentication tokens/file is available
if [[ -z "${IQM_TOKEN:-}" && -z "${test_tokens_file}" && -z "${IQM_TOKENS_FILE:-}" ]]; then
  echo "ERROR: Either IQM_TOKEN or IQM_TOKENS_FILE must be set in the environment or passed as an argument." >&2
  exit 1
fi

echo "=== IQM SPANK Plugin: Resonance Workflow Verification ===" >&2
echo "" >&2

# Check connectivity
echo "Checking Slurm connectivity..." >&2
if ! connectivity_output=$("$srun_cmd" --immediate=3 -N1 -n1 /bin/true 2>&1); then
  echo "ERROR: Cannot allocate a job via srun. Ensure Slurm is running." >&2
  echo "       Output: $connectivity_output" >&2
  exit 1
fi
echo "OK: Slurm is reachable." >&2

# Assemble srun arguments
srun_args=(--immediate=300 -N1 -n1) # longer timeout for real workflow / remote API
if [[ -n "$partition" ]]; then
  srun_args+=(--partition "$partition")
fi

srun_args+=(
  "--iqm-base-url=$test_base_url"
  "--iqm-qc-alias=$test_qc_alias"
)

if [[ -n "$test_tokens_file" ]]; then
  srun_args+=("--iqm-tokens-file=$test_tokens_file")
fi

# Locate project root (one directory up from where this script is located inside spank/test/)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "Running bell_state.py under srun..." >&2
echo "Command: $srun_cmd ${srun_args[*]} uv run --project $PROJECT_ROOT $PROJECT_ROOT/spank/test/bell_state.py" >&2

# Run the python script under srun
set +e
"$srun_cmd" "${srun_args[@]}" \
  uv run --project "$PROJECT_ROOT" \
  "$PROJECT_ROOT/spank/test/bell_state.py"
exit_code=$?
set -e

if [[ $exit_code -ne 0 ]]; then
  echo "FAIL: Workflow execution failed (exit code $exit_code)" >&2
  exit $exit_code
fi

echo "PASS: Resonance workflow completed successfully." >&2
exit 0
