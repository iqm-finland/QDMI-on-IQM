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

"""PennyLane adapter for the IQM QDMI device."""

from __future__ import annotations

from typing import TYPE_CHECKING

from mqt.core.plugins.pennylane.device import QDMIDevice

from . import IQM_QDMI_DEVICE_ID
from ._session import session_parameters

if TYPE_CHECKING:
    import os
    from collections.abc import Hashable, Sequence
    from uuid import UUID

    from mqt.core.typing import QDMIJobParameters


class IQMDevice(QDMIDevice):
    """Execute PennyLane circuits on an IQM device through MQT Core.

    Args:
        device_id: Stable ID from the installed IQM catalogue.
        wires: PennyLane qubit labels or qubit count.
        base_url: IQM service endpoint.
        token: Authentication token; defaults to ``IQM_TOKEN``.
        tokens_file: Authentication file; defaults to ``IQM_TOKENS_FILE``.
        qc_id: Quantum computer identifier.
        qc_alias: Quantum computer alias.
        calibration_set_id: Calibration UUID used for compilation and execution.
        job_parameters: QDMI job settings, including IQM run options in ``custom1``.

    Endpoint and computer environment defaults apply to ``iqm.default``.
    Named devices use their manifest settings unless explicitly overridden.
    """

    def __init__(
        self,
        device_id: str = IQM_QDMI_DEVICE_ID,
        wires: int | Sequence[Hashable] | None = None,
        *,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
        calibration_set_id: str | UUID | None = None,
        job_parameters: QDMIJobParameters | None = None,
    ) -> None:
        """Open an IQM session with the shared MQT Core execution device."""
        super().__init__(
            device_id,
            wires=wires,
            session_parameters=session_parameters(
                device_id,
                base_url=base_url,
                token=token,
                tokens_file=tokens_file,
                qc_id=qc_id,
                qc_alias=qc_alias,
                calibration_set_id=calibration_set_id,
            ),
            job_parameters=job_parameters,
        )
