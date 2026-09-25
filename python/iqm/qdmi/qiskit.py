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
from typing import TYPE_CHECKING, ClassVar

try:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from mqt.core.qdmi.builtin_driver import open_device
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from . import IQM_QDMI_DEVICE_ID
from .gates import MoveGate

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from mqt.core.qdmi import Device
    from qiskit.circuit import Instruction

__all__ = ["IQMBackend"]


def __dir__() -> list[str]:
    return __all__


class IQMBackend(QDMIBackend):
    """Qiskit backend for the packaged IQM QDMI device library.

    This backend loads the shared library distributed with `iqm-qdmi` and
    exposes it through MQT Core's Qiskit-compatible QDMI backend.

    Args:
        device_id: Stable ID from the installed catalogue. Defaults to `iqm.default`.
        device: An already-open device, including one supplied by `from_device_id`.
        provider: Optional Qiskit provider to associate with this backend.
        base_url: Base URL of the IQM service. Overrides `IQM_SERVER_URL`, its
            `IQM_BASE_URL` alias, and the manifest default when provided.
        token: Authentication token. Defaults to `IQM_TOKEN`.
        tokens_file: Path to an authentication file. Defaults to `IQM_TOKENS_FILE`.
        qc_id: Optional IQM quantum computer identifier. Defaults to `IQM_QC_ID`.
        qc_alias: Optional IQM quantum computer alias. Defaults to
            `IQM_QUANTUM_COMPUTER`, then its `IQM_QC_ALIAS` alias.

    Environment defaults for the endpoint and quantum computer apply only to
    `iqm.default`. An explicit ID or alias takes precedence over either selector
    from the environment. Named presets use their manifest configuration.
    """

    #: MOVE is native to IQM's star-topology devices but absent from Qiskit's
    #: standard gate library, so the Target needs it supplied here.
    _EXTRA_GATES: ClassVar[dict[str, Instruction | type[Instruction]]] = {"move": MoveGate()}

    def __init__(
        self,
        device_id: str | None = None,
        *,
        device: Device | None = None,
        provider: QDMIProvider | None = None,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
    ) -> None:
        """Initialize the IQM Qiskit backend.

        Raises:
            ValueError: If an already-open device is combined with session overrides.
        """
        if device is not None:
            if any(value is not None for value in (base_url, token, tokens_file, qc_id, qc_alias)):
                msg = "An already-open device cannot be combined with session overrides."
                raise ValueError(msg)
        else:
            device_id = device_id or IQM_QDMI_DEVICE_ID
            if device_id == IQM_QDMI_DEVICE_ID:
                base_url = base_url or os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL")
                if not qc_id and not qc_alias:
                    qc_id = os.getenv("IQM_QC_ID")
                    qc_alias = os.getenv("IQM_QUANTUM_COMPUTER") or os.getenv("IQM_QC_ALIAS")
            tokens_file_value = tokens_file or os.getenv("IQM_TOKENS_FILE")
            device = open_device(
                device_id,
                base_url=base_url or None,
                token=token or os.getenv("IQM_TOKEN"),
                auth_file=Path(tokens_file_value) if tokens_file_value else None,
                custom1=qc_id,
                custom2=qc_alias,
            )
        super().__init__(device=device, provider=provider, device_id=device_id)
