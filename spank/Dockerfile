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

FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    ca-certificates \
    libcurl4-openssl-dev \
    libslurm-dev \
    slurm-wlm \
    munge \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Install uv and uvx from the official image
COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /usr/local/bin/

# Create a non-root user 'testuser' with UID 1000 and passwordless sudo rights
RUN userdel -r ubuntu || true
RUN useradd -m -u 1000 -s /bin/bash testuser && \
    echo "testuser ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Create Slurm state, spool, and log directories/files with appropriate ownership/permissions
RUN mkdir -p /var/spool/slurmctld /var/spool/slurmd && \
    chown slurm:slurm /var/spool/slurmctld && \
    chown root:root /var/spool/slurmd && \
    chmod 755 /var/spool/slurmctld /var/spool/slurmd && \
    touch /var/log/slurmctld.log /var/log/slurmd.log && \
    chown slurm:slurm /var/log/slurmctld.log && \
    chown root:root /var/log/slurmd.log

# Configure Slurm with a static localhost setup
RUN echo "ClusterName=local-ci" > /etc/slurm/slurm.conf && \
    echo "SlurmctldHost=localhost(127.0.0.1)" >> /etc/slurm/slurm.conf && \
    echo "SlurmUser=slurm" >> /etc/slurm/slurm.conf && \
    echo "SlurmctldPort=6817" >> /etc/slurm/slurm.conf && \
    echo "SlurmdPort=6818" >> /etc/slurm/slurm.conf && \
    echo "AuthType=auth/munge" >> /etc/slurm/slurm.conf && \
    echo "StateSaveLocation=/var/spool/slurmctld" >> /etc/slurm/slurm.conf && \
    echo "SlurmdSpoolDir=/var/spool/slurmd" >> /etc/slurm/slurm.conf && \
    echo "SlurmdLogFile=/var/log/slurmd.log" >> /etc/slurm/slurm.conf && \
    echo "SlurmctldLogFile=/var/log/slurmctld.log" >> /etc/slurm/slurm.conf && \
    echo "ProctrackType=proctrack/pgid" >> /etc/slurm/slurm.conf && \
    echo "SchedulerType=sched/backfill" >> /etc/slurm/slurm.conf && \
    echo "SelectType=select/linear" >> /etc/slurm/slurm.conf && \
    echo "TaskPlugin=task/none" >> /etc/slurm/slurm.conf && \
    echo "SlurmdParameters=config_overrides" >> /etc/slurm/slurm.conf && \
    echo "PlugStackConfig=/etc/slurm/plugstack.conf" >> /etc/slurm/slurm.conf && \
    echo "NodeName=localhost NodeAddr=127.0.0.1 State=UNKNOWN" >> /etc/slurm/slurm.conf && \
    echo "PartitionName=debug Nodes=localhost Default=YES MaxTime=INFINITE State=UP" >> /etc/slurm/slurm.conf

# Force Slurm to use cgroup/v1 backend (which won't fail inside unprivileged Docker containers)
RUN echo "CgroupPlugin=cgroup/v1" > /etc/slurm/cgroup.conf

# Set up plugstack configuration directories
RUN mkdir -p /etc/slurm/plugstack.conf.d && \
    echo "include /etc/slurm/plugstack.conf.d/*.conf" > /etc/slurm/plugstack.conf

# Create workspace directory and set ownership
RUN mkdir -p /workspace && chown -R testuser:testuser /workspace

WORKDIR /workspace

# Copy the workspace into the container (used when building the self-contained image)
COPY --chown=testuser:testuser . /workspace

# Change user to testuser
USER testuser

# Warm up the uv cache with project dependencies
RUN uv sync --all-extras --dev --no-install-project

# Set entrypoint to run the tests
ENTRYPOINT ["/workspace/spank/test/run_docker_tests.sh"]
