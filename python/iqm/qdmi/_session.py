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

"""IQM session settings shared by the Python adapters."""

from __future__ import annotations

import os
from pathlib import Path
from typing import TYPE_CHECKING
from uuid import UUID

from . import IQM_QDMI_DEVICE_ID

if TYPE_CHECKING:
    from mqt.core.typing import QDMISessionParameters


def session_parameters(
    device_id: str,
    *,
    base_url: str | None = None,
    token: str | None = None,
    tokens_file: str | os.PathLike[str] | None = None,
    qc_id: str | None = None,
    qc_alias: str | None = None,
    calibration_set_id: str | UUID | None = None,
) -> QDMISessionParameters:
    """Resolve IQM environment defaults and explicit session settings.

    Returns:
        QDMI parameters; named devices retain their manifest endpoint and selector.
    """
    calibration_id = str(UUID(str(calibration_set_id))) if calibration_set_id is not None else None
    if device_id == IQM_QDMI_DEVICE_ID:
        base_url = base_url or os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL")
        if not qc_id and not qc_alias:
            qc_id = os.getenv("IQM_QC_ID")
            qc_alias = os.getenv("IQM_QUANTUM_COMPUTER") or os.getenv("IQM_QC_ALIAS")
    tokens_file_value = tokens_file or os.getenv("IQM_TOKENS_FILE")
    return {
        "base_url": base_url or None,
        "token": token or os.getenv("IQM_TOKEN") or None,
        "auth_file": Path(tokens_file_value) if tokens_file_value else None,
        "custom1": qc_id,
        "custom2": qc_alias,
        "custom4": calibration_id,
    }
