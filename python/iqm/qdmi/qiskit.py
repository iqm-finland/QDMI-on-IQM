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

"""Qiskit-facing integration for the IQM QDMI device library."""

from __future__ import annotations

import os
from pathlib import Path
from typing import TYPE_CHECKING, Any, ClassVar

try:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from mqt.core.plugins.qiskit.estimator import QDMIEstimator
    from mqt.core.plugins.qiskit.sampler import QDMISampler
    from mqt.core.qdmi.driver import DeviceDefinition, open_device, register_device_if_absent
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from . import IQM_QDMI_DEVICE_ID, IQM_QDMI_LIBRARY_PATH, IQM_QDMI_PREFIX
from .gates import MoveGate

if TYPE_CHECKING:
    from qiskit.circuit import Instruction

__all__ = ["IQMBackend"]

IQM_DEFAULT_BASE_URL = "https://resonance.iqm.tech"


def __dir__() -> list[str]:
    return __all__


class IQMBackend(QDMIBackend):
    """Qiskit backend for the packaged IQM QDMI device library.

    This backend loads the shared library distributed with `iqm-qdmi` and
    exposes it through MQT Core's Qiskit-compatible QDMI backend.

    Args:
        base_url: Base URL of the IQM service. Overrides `IQM_BASE_URL` and
            the registered device default when provided.
        token: Authentication token. Defaults to `IQM_TOKEN`.
        tokens_file: Path to an authentication file. Defaults to `IQM_TOKENS_FILE`.
        qc_id: Optional IQM quantum computer identifier. Defaults to `IQM_QC_ID`.
        qc_alias: Optional IQM quantum computer alias. Defaults to `IQM_QC_ALIAS`.
    """

    #: MOVE is native to IQM's star-topology devices but absent from Qiskit's
    #: standard gate library, so the Target needs it supplied here.
    _EXTRA_GATES: ClassVar[dict[str, Instruction | type[Instruction]]] = {"move": MoveGate()}

    def __init__(
        self,
        *,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
    ) -> None:
        """Initialize the IQM Qiskit backend."""
        resolved_base_url = base_url or os.getenv("IQM_BASE_URL") or None
        resolved_token = token or os.getenv("IQM_TOKEN")
        tokens_file_value = tokens_file or os.getenv("IQM_TOKENS_FILE")
        resolved_tokens_file = Path(tokens_file_value) if tokens_file_value else None
        resolved_qc_id = qc_id or os.getenv("IQM_QC_ID")
        resolved_qc_alias = qc_alias or os.getenv("IQM_QC_ALIAS")

        register_device_if_absent(
            DeviceDefinition(
                IQM_QDMI_DEVICE_ID,
                IQM_QDMI_LIBRARY_PATH,
                IQM_QDMI_PREFIX,
                base_url=IQM_DEFAULT_BASE_URL,
            )
        )
        device = open_device(
            IQM_QDMI_DEVICE_ID,
            base_url=resolved_base_url,
            token=resolved_token,
            auth_file=resolved_tokens_file,
            custom1=resolved_qc_id,
            custom2=resolved_qc_alias,
        )
        super().__init__(device=device)

    def sampler(
        self,
        *,
        default_shots: int = 1024,
        options: dict[str, Any] | None = None,
    ) -> QDMISampler:
        """Returns SamplerV2 primitive bound to this backend."""
        return QDMISampler(self, default_shots=default_shots, options=options)

    def estimator(
        self,
        *,
        default_precision: float = 0.0,
        options: dict[str, Any] | None = None,
    ) -> QDMIEstimator:
        """Returns an EstimatorV2 primitive bound to this backend."""
        return QDMIEstimator(self, default_precision=default_precision, options=options)
