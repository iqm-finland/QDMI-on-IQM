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

# Entrypoint script for starting Slurm SPANK test container services.
set -euo pipefail

# Detect bind-mounted workspace and rebuild/reinstall plugin if present
if mountpoint -q /workspace; then
  echo "=== Bind-mounted workspace detected. Rebuilding/reinstalling SPANK plugin... ==="
  cmake -S /workspace -B /workspace/build-spank-docker -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_SPANK=ON \
    -DIQM_QDMI_SPANK_INSTALL_DIR=/usr/lib/x86_64-linux-gnu/slurm-wlm -DIQM_QDMI_SLURM_CONF_DIR=/etc/slurm
  cmake --build /workspace/build-spank-docker --target iqm-spank-plugin
  sudo cmake --install /workspace/build-spank-docker --component iqm-spank-plugin

  # The CMake installation writes a commented-out template of iqm-qdmi.conf.
  # We must activate it by writing the uncommented, active required line.
  echo "required /usr/lib/x86_64-linux-gnu/slurm-wlm/iqm-spank-plugin.so iqm_base_url=https://resonance.iqm.tech" \
    | sudo tee /etc/slurm/plugstack.conf.d/iqm-qdmi.conf
fi

# Start Munge service
echo "=== Starting Munge service ==="
sudo service munge start

# Start Slurmctld
echo "=== Starting Slurmctld ==="
sudo /usr/sbin/slurmctld || { echo "ERROR: slurmctld failed to start"; exit 1; }

# Start Slurmd
echo "=== Starting Slurmd ==="
sudo /usr/sbin/slurmd -N localhost || { echo "ERROR: slurmd failed to start"; exit 1; }

# Wait for node to be ready
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

exec "$@"
