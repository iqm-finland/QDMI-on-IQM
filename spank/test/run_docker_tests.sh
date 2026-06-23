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

# Entrypoint script for running Slurm SPANK plugin tests in Docker.
set -euo pipefail

# Helper to dump log files in case of failure
show_logs() {
  echo "=== Slurmctld Status ==="
  sudo service slurmctld status || true
  echo "=== Slurmd Status ==="
  sudo pgrep -af slurmd || true
  echo "=== Munge Status ==="
  sudo service munge status || true
  echo "=== Slurmctld Log ==="
  sudo cat /var/log/slurmctld.log || true
  echo "=== Slurmd Log ==="
  sudo cat /var/log/slurmd.log || true
}

# If any command fails, dump logs before exiting
trap show_logs ERR

echo "=== Starting Munge service ==="
sudo service munge start

echo "=== Starting Slurmctld ==="
sudo /usr/sbin/slurmctld || { echo "ERROR: slurmctld failed to start"; exit 1; }

echo "=== Starting Slurmd ==="
# Slurmd needs to be started as root, bound to localhost.
sudo /usr/sbin/slurmd -N localhost || { echo "ERROR: slurmd failed to start"; exit 1; }

echo "=== Waiting for Slurm node to become ready ==="
for i in {1..30}; do
  if sinfo -h -n "localhost" -o "%t" | grep -qE "idle|alloc"; then
    break
  fi
  # Force resume if the node got marked DOWN/DRAINED
  sudo scontrol update nodename="localhost" state=resume 2>/dev/null || true
  sleep 1
done

if ! sinfo -h -n "localhost" -o "%t" | grep -qE "idle|alloc"; then
  echo "ERROR: Node did not become ready after 30 seconds"
  exit 1
fi

echo "=== Verifying basic Slurm srun connectivity ==="
srun --partition=debug --immediate=5 /bin/true

echo "=== Configuring CMake ==="
cmake -S /workspace -B /workspace/build-spank-docker -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_SPANK=ON

echo "=== Building SPANK plugin ==="
cmake --build /workspace/build-spank-docker --target iqm-spank-plugin

echo "=== Installing SPANK plugin ==="
sudo cmake --install /workspace/build-spank-docker --component iqm-spank-plugin

echo "=== Activating SPANK plugin ==="
PLUGIN_DIR=$(scontrol show config | sed -n 's/^PluginDir *= *//p')
PLUGIN_PATH="${PLUGIN_DIR}/iqm-spank-plugin.so"
TEST_BASE_URL="https://test.example.com"
echo "required ${PLUGIN_PATH} iqm_base_url=${TEST_BASE_URL}" \
  | sudo tee /etc/slurm/plugstack.conf.d/iqm-qdmi.conf

# Reload Slurm configuration
sudo scontrol reconfigure
sleep 2

echo "=== Running SPANK smoke tests ==="
uv run nox -f /workspace/spank/noxfile.py --envdir /home/testuser/.nox -s smoke_tests -- \
  --partition debug \
  --hook-mode full \
  --require-all-hooks \
  --test-base-url "$TEST_BASE_URL"

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
  uv run nox -f /workspace/spank/noxfile.py --envdir /home/testuser/.nox -s resonance_tests -- "${RESONANCE_ARGS[@]}"
fi

# Disable the error trap before exiting cleanly
trap - ERR
echo "=== All SPANK tests completed successfully! ==="
