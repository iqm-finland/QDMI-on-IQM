# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Qiskit-facing integration for the IQM QDMI device library."""

from __future__ import annotations

import os
from typing import Any

from mqt.core import fomac
from mqt.core.plugins.qiskit import QDMIBackend, QDMIEstimator, QDMISampler

from ._paths import IQM_QDMI_LIBRARY_PATH

__all__ = ["IQMBackend"]


def __dir__() -> list[str]:
    return __all__


class IQMBackend(QDMIBackend):
    """Qiskit backend for the packaged IQM QDMI device library.

    This backend loads the shared library distributed with ``iqm-qdmi`` and
    exposes it through MQT Core's Qiskit-compatible QDMI backend.

    Args:
        base_url: Base URL of the IQM service. Defaults to ``IQM_BASE_URL`` or
            the standard Resonance endpoint.
        token: Authentication token. Defaults to ``RESONANCE_API_KEY``.
        auth_file: Path to an authentication file.
        auth_url: Authentication server URL.
        username: Username for authentication.
        password: Password for authentication.
        qc_id: Optional IQM quantum computer identifier.
        qc_alias: Optional IQM quantum computer alias.
    """

    def __init__(
        self,
        *,
        base_url: str | None = None,
        token: str | None = None,
        auth_file: str | None = None,
        auth_url: str | None = None,
        username: str | None = None,
        password: str | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
    ) -> None:
        """Initialize the IQM Qiskit backend."""
        device = fomac.add_dynamic_device_library(
            library_path=str(IQM_QDMI_LIBRARY_PATH),
            prefix="IQM",
            base_url=base_url or os.getenv("IQM_BASE_URL") or "https://resonance.meetiqm.com",
            token=token or os.getenv("RESONANCE_API_KEY"),
            auth_file=auth_file,
            auth_url=auth_url,
            username=username,
            password=password,
            custom1=qc_id or os.getenv("IQM_QC_ID"),
            custom2=qc_alias or os.getenv("IQM_QC_ALIAS"),
        )
        super().__init__(device=device)

    def sampler(
        self,
        *,
        default_shots: int = 1024,
        options: dict[str, Any] | None = None,
    ) -> QDMISampler:
        """Create a sampler primitive bound to this backend.

        Returns:
            A sampler primitive configured to submit circuits through this backend.
        """
        return QDMISampler(self, default_shots=default_shots, options=options)

    def estimator(
        self,
        *,
        default_precision: float = 0.0,
        options: dict[str, Any] | None = None,
    ) -> QDMIEstimator:
        """Create an estimator primitive bound to this backend.

        Returns:
            An estimator primitive configured to submit observables through this backend.
        """
        return QDMIEstimator(self, default_precision=default_precision, options=options)
