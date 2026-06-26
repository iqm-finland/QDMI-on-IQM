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

# Script to run Slurm SPANK plugin tests inside a pre-configured Docker container.
set -euo pipefail

# Helper to dump log files in case of failure
show_logs() {
  echo "=== Slurmctld Log ==="
  sudo cat /var/log/slurmctld.log || true
  echo "=== Slurmd Log ==="
  sudo cat /var/log/slurmd.log || true
}

# If any command fails, dump logs before exiting
trap show_logs ERR

echo "=== Verifying basic Slurm srun connectivity ==="
srun --partition=debug --immediate=5 /bin/true

# Change directory to the spank folder so nox automatically resolves noxfile.py
cd /workspace/spank

# Running smoke tests
TEST_BASE_URL="https://resonance.iqm.tech"
echo "=== Running SPANK smoke tests ==="
uvx nox -s smoke_tests -- \
  --partition debug \
  --hook-mode full \
  --require-all-hooks \
  --test-base-url "$TEST_BASE_URL"

# Running resonance tests if authentication is configured
if [[ -n "${IQM_TOKEN:-}" || -n "${IQM_TOKENS_FILE:-}" ]]; then
  echo "=== Running SPANK resonance tests ==="
  RESONANCE_ARGS=(
    "--partition" "debug"
    "--test-base-url" "https://resonance.iqm.tech"
    "--test-qc-alias" "emerald:mock"
  )
  if [[ -n "${IQM_TOKENS_FILE:-}" ]]; then
    RESONANCE_ARGS+=("--test-tokens-file" "${IQM_TOKENS_FILE}")
  fi
  uvx nox -s resonance_tests -- "${RESONANCE_ARGS[@]}"
fi

# Disable the error trap before exiting cleanly
trap - ERR
echo "=== All SPANK tests completed successfully! ==="
