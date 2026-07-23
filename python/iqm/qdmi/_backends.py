# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Shared backend/primitive construction for the offloader and its Slurm CLI workers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from mqt.core.plugins.qiskit.estimator import QDMIEstimator
from mqt.core.plugins.qiskit.provider import QDMIProvider
from mqt.core.plugins.qiskit.sampler import QDMISampler

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend

_SIMULATOR_DEVICE = "MQT Core DDSIM QDMI Device"

#: Optimization level used whenever a circuit is transpiled for execution
#: (offloader local path and the `iqm-sampler` Slurm worker), so the two
#: paths stay in sync instead of drifting to different Qiskit defaults.
TRANSPILE_OPTIMIZATION_LEVEL = 2


def build_sampler(
    *,
    simulator: bool,
    base_url: str | None = None,
    tokens_file: str | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
) -> tuple[QDMIBackend, QDMISampler]:
    """Build the backend and Sampler primitive to use for a sampling job.

    Returns:
        A tuple of the resolved backend and a Sampler primitive bound to it.
    """
    if simulator:
        backend = QDMIProvider().get_backend(_SIMULATOR_DEVICE)
        return backend, QDMISampler(backend)

    backend = IQMBackend(base_url=base_url, tokens_file=tokens_file, qc_id=qc_id, qc_alias=qc_alias)
    return backend, backend.sampler()


def build_estimator(
    *,
    simulator: bool,
    base_url: str | None = None,
    tokens_file: str | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
) -> tuple[QDMIBackend, QDMIEstimator]:
    """Build the backend and Estimator primitive to use for a VQE estimation job.

    Returns:
        A tuple of the resolved backend and an Estimator primitive bound to it.
    """
    if simulator:
        backend = QDMIProvider().get_backend(_SIMULATOR_DEVICE)
        return backend, QDMIEstimator(backend)

    backend = IQMBackend(base_url=base_url, tokens_file=tokens_file, qc_id=qc_id, qc_alias=qc_alias)
    return backend, backend.estimator()
