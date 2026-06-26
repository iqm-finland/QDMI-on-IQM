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
COPY spank/config/slurm.conf /etc/slurm/slurm.conf
COPY spank/config/cgroup.conf /etc/slurm/cgroup.conf

# Set up plugstack configuration directories
RUN mkdir -p /etc/slurm/plugstack.conf.d && \
    echo "include /etc/slurm/plugstack.conf.d/*.conf" > /etc/slurm/plugstack.conf

# Create workspace directory and set ownership
RUN mkdir -p /workspace && chown -R testuser:testuser /workspace

WORKDIR /workspace

# Copy the workspace into the container (used when building the self-contained image)
COPY --chown=testuser:testuser . /workspace

# Build, install, and globally activate the SPANK plugin at image build time
# Also derive the Slurm multiarch library directory dynamically
RUN SLURM_LIB_DIR=$(dpkg-architecture -qDEB_HOST_MULTIARCH | xargs -I{} echo /usr/lib/{}/slurm-wlm) && \
    cmake -S /workspace -B /workspace/build-spank-docker -DCMAKE_BUILD_TYPE=Release -DBUILD_IQM_SPANK=ON \
    -DIQM_QDMI_SPANK_INSTALL_DIR="$SLURM_LIB_DIR" -DIQM_QDMI_SLURM_CONF_DIR=/etc/slurm && \
    cmake --build /workspace/build-spank-docker --target iqm-spank-plugin && \
    cmake --install /workspace/build-spank-docker --component iqm-spank-plugin && \
    rm -rf /workspace/build-spank-docker && \
    echo "required $SLURM_LIB_DIR/iqm-spank-plugin.so iqm_base_url=https://resonance.iqm.tech" > /etc/slurm/plugstack.conf.d/iqm-qdmi.conf

# Create a sentinel file to detect bind-mount shadowing
RUN touch /workspace/.built-in-image

# Change user to testuser
USER testuser

# Warm up the uv cache with project dependencies (production only, no dev/docs/lint to save space)
RUN uv sync --no-dev --no-install-project

# Set entrypoint to run the services and command
ENTRYPOINT ["/workspace/spank/test/entrypoint.sh"]
CMD ["/workspace/spank/test/run_docker_tests.sh"]
