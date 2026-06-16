#!/usr/bin/env bash
# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

set -euo pipefail

partition=""
require_all_hooks=0
srun_cmd="srun"
job_command="/bin/true"
hook_mode="local"

detect_plugstack_config() {
  if ! command -v scontrol >/dev/null 2>&1; then
    return 0
  fi

  local plugstack_path
  plugstack_path="$(scontrol show config 2>/dev/null | awk -F'= *' '/^PlugStackConfig/ {print $2}' | xargs || true)"
  if [[ -z "$plugstack_path" ]]; then
    return 0
  fi

  if [[ ! -f "$plugstack_path" ]]; then
    echo "WARNING: Active PlugStackConfig path does not exist: $plugstack_path" >&2
    if [[ -f /etc/slurm-llnl/plugstack.conf ]]; then
      echo "Hint: you appear to use /etc/slurm-llnl. Either point Slurm to that file" >&2
      echo "      or create /etc/slurm/plugstack.conf as a symlink." >&2
    fi
    return 0
  fi

  # Build effective plugstack content by resolving 'include' directives.
  local effective_content
  effective_content="$(awk '!/^\s*#/ && NF' "$plugstack_path")"

  local include_glob
  include_glob="$(echo "$effective_content" | awk '/^include / {print $2}')"
  if [[ -n "$include_glob" ]]; then
    local inc_file
    for inc_file in $include_glob; do
      [[ -f "$inc_file" ]] || continue
      effective_content+=$'\n'"$(awk '!/^\s*#/ && NF' "$inc_file")"
    done
  fi

  local configured_plugin
  configured_plugin="$(echo "$effective_content" | awk '{print $NF}' | grep -E 'iqm.*spank.*\.so$' | tail -n 1 || true)"
  if [[ -z "$configured_plugin" ]]; then
    echo "WARNING: No IQM SPANK plugin entry found in $plugstack_path" >&2
    return 0
  fi

  if [[ ! -f "$configured_plugin" ]]; then
    echo "WARNING: Plugstack references missing plugin file: $configured_plugin" >&2
  fi
}

check_daemon_logs_for_hooks() {
  if ! command -v journalctl >/dev/null 2>&1; then
    return 1
  fi

  local daemon_log
  daemon_log="$(journalctl -u slurmd --since '-2 min' --no-pager 2>/dev/null || true)"
  if [[ -z "$daemon_log" ]]; then
    return 1
  fi

  if grep -Fq "[iqm_spank_plugin] hook=" <<<"$daemon_log"; then
    echo "INFO: Hook logs found in slurmd journal (not forwarded to srun output)." >&2
    grep -F "[iqm_spank_plugin] hook=" <<<"$daemon_log" >&2 || true
    return 0
  fi

  return 1
}

usage() {
  cat <<'EOF'
Usage: spank/test/test_hook_logs.sh [options]

Runs a Slurm smoke test and checks for iqm_spank_plugin hook log lines.

Options:
  --partition <name>       Partition to use (passed to srun as --partition).
  --srun <path>            srun binary to use (default: srun).
  --command <cmd>          Command to run via srun (default: /bin/true).
  --hook-mode <mode>       One of: local, full (default: local).
  --require-all-hooks      Require all known hooks to appear in captured output.
  -h, --help               Show this help.

Examples:
  spank/test/test_hook_logs.sh
  spank/test/test_hook_logs.sh --partition quantum
  spank/test/test_hook_logs.sh --partition quantum --hook-mode full
  spank/test/test_hook_logs.sh --partition quantum --require-all-hooks
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
    --command)
      job_command="$2"
      shift 2
      ;;
    --hook-mode)
      hook_mode="$2"
      shift 2
      ;;
    --require-all-hooks)
      require_all_hooks=1
      shift
      ;;
    -h|--help)
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

if [[ "$hook_mode" != "local" && "$hook_mode" != "full" ]]; then
  echo "ERROR: --hook-mode must be either 'local' or 'full'." >&2
  exit 2
fi

if [[ $require_all_hooks -eq 1 ]]; then
  hook_mode="full"
fi

if ! command -v "$srun_cmd" >/dev/null 2>&1; then
  echo "ERROR: '$srun_cmd' not found in PATH." >&2
  exit 127
fi

detect_plugstack_config

srun_args=(--immediate=3 -N1 -n1)
if [[ -n "$partition" ]]; then
  srun_args+=(--partition "$partition")
fi
srun_args+=("$job_command")

echo "Running: $srun_cmd ${srun_args[*]}" >&2

tmp_log_file="$(mktemp)"
trap 'rm -f "$tmp_log_file"' EXIT

set +e
"$srun_cmd" "${srun_args[@]}" >"$tmp_log_file" 2>&1
srun_exit_code=$?
set -e

cat "$tmp_log_file"

local_hooks=(
  "slurm_spank_init"
  "slurm_spank_init_post_opt"
)

full_hooks=(
  "slurm_spank_init"
  "slurm_spank_init_post_opt"
  "slurm_spank_task_init"
  "slurm_spank_task_init_privileged"
  "slurm_spank_exit"
)

expected_hooks=("${local_hooks[@]}")
if [[ "$hook_mode" == "full" ]]; then
  expected_hooks=("${full_hooks[@]}")
fi

seen_hooks=()
for hook_name in "${expected_hooks[@]}"; do
  if grep -Fq "[iqm_spank_plugin] hook=${hook_name}" "$tmp_log_file"; then
    seen_hooks+=("$hook_name")
  fi
done

if [[ $srun_exit_code -ne 0 ]]; then
  if [[ "$hook_mode" == "local" && ${#seen_hooks[@]} -gt 0 ]]; then
    echo "PASS: Detected local SPANK hooks despite allocation failure: ${seen_hooks[*]}" >&2
    exit 0
  fi

  echo "ERROR: srun failed (exit code $srun_exit_code)." >&2
  echo "Hint: ensure Slurm is configured and reachable from this host." >&2
  exit $srun_exit_code
fi

if [[ $require_all_hooks -eq 1 ]]; then
  if [[ ${#seen_hooks[@]} -ne ${#expected_hooks[@]} ]]; then
    echo "ERROR: Not all expected hook logs were found." >&2
    echo "Seen hooks (${#seen_hooks[@]}/${#expected_hooks[@]}): ${seen_hooks[*]:-none}" >&2
    exit 1
  fi
  echo "PASS: All expected SPANK hook logs were detected." >&2
  exit 0
fi

if [[ ${#seen_hooks[@]} -eq 0 ]]; then
  if check_daemon_logs_for_hooks; then
    echo "PASS: SPANK hook logs detected via slurmd journal." >&2
    exit 0
  fi

  echo "ERROR: No SPANK hook log lines were detected in srun output." >&2
  echo "Hint: verify active PlugStackConfig path and check slurmd logs." >&2
  exit 1
fi

echo "PASS: Detected SPANK hook logs: ${seen_hooks[*]}" >&2
