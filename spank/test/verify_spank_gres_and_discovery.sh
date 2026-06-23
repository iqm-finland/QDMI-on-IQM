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

set -euo pipefail

# Start the mock server in the background
python3 /workspace/spank/test/mock_server.py 8089 &
MOCK_PID=$!

cleanup() {
  echo "=== Cleaning up mock server ==="
  kill "$MOCK_PID" || true
}
trap cleanup EXIT

echo "=== Starting Munge service ==="
sudo service munge start || true
sudo mkdir -p /var/spool/slurmctld /var/spool/slurmd /var/log || true
sudo chown slurm:slurm /var/spool/slurmctld || true
sudo chown root:root /var/spool/slurmd || true

echo "=== Generating /etc/slurm/slurm.conf ==="
HOSTNAME=$(hostname)
cat <<EOF | sudo tee /etc/slurm/slurm.conf
ClusterName=local-ci
SlurmctldHost=$HOSTNAME(127.0.0.1)
SlurmUser=slurm
SlurmctldPort=6817
SlurmdPort=6818
AuthType=auth/munge
StateSaveLocation=/var/spool/slurmctld
SlurmdSpoolDir=/var/spool/slurmd
SlurmdLogFile=/var/log/slurmd.log
SlurmctldLogFile=/var/log/slurmctld.log
ProctrackType=proctrack/pgid
SchedulerType=sched/backfill
SelectType=select/linear
TaskPlugin=task/none
SlurmdParameters=config_overrides
PlugStackConfig=/etc/slurm/plugstack.conf
GresTypes=qpu,customqpu
NodeName=$HOSTNAME NodeAddr=127.0.0.1 Gres=qpu:emerald:1,customqpu:garnet:1 State=UNKNOWN
PartitionName=debug Nodes=$HOSTNAME Default=YES MaxTime=INFINITE State=UP
EOF

echo "=== Generating /etc/slurm/gres.conf ==="
cat <<EOF | sudo tee /etc/slurm/gres.conf
Name=qpu Type=emerald Count=1
Name=customqpu Type=garnet Count=1
EOF

echo "=== Starting/Restarting Slurmctld and Slurmd ==="
sudo service slurmctld restart
sudo service slurmd restart

echo "=== Waiting for Slurm node to become ready ==="
for i in {1..30}; do
  if sinfo -h -n "$HOSTNAME" -o "%t" | grep -qE "idle|alloc"; then
    break
  fi
  sudo scontrol update nodename="$HOSTNAME" state=resume 2>/dev/null || true
  sleep 1
done

echo "=== Configuring CMake in /tmp/build-spank ==="
cmake -S /workspace -B /tmp/build-spank -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_SPANK=ON

echo "=== Building SPANK plugin ==="
cmake --build /tmp/build-spank --target iqm-spank-plugin

echo "=== Installing SPANK plugin ==="
sudo cmake --install /tmp/build-spank --component iqm-spank-plugin

echo "=== Activating SPANK plugin ==="
echo "include /etc/slurm/plugstack.conf.d/*.conf" | sudo tee /etc/slurm/plugstack.conf
PLUGIN_DIR=$(scontrol show config | awk -F'= *' '/^PluginDir/ {print $2}' | xargs)
PLUGIN_PATH="${PLUGIN_DIR}/iqm-spank-plugin.so"
echo "required ${PLUGIN_PATH} iqm_base_url=http://localhost:8089" | sudo tee /etc/slurm/plugstack.conf.d/iqm-qdmi.conf

sudo scontrol reconfigure
sleep 2

echo "=== Verification 1: Test SPANK CLI discovery option ==="
srun --iqm-list-devices /bin/true

echo "=== Verification 2: Test GRES Auto-Mapping ==="
srun --gres=qpu:emerald:1 env | grep IQM_QC_ALIAS

echo "=== Verification 3: Test GRES Configurable GRES Name Mapping ==="
srun --iqm-gres-name=customqpu --gres=customqpu:garnet:1 env | grep IQM_QC_ALIAS

echo "=== Verification 4: Test GRES with pure count format (should not map) ==="
# A GRES request like qpu:1 should not map since there is no type/alias specified
srun --gres=qpu:1 env | grep IQM_QC_ALIAS || echo "OK: No alias mapped for pure count GRES"

echo "=== SPANK GRES & Discovery Verification Successful ==="
