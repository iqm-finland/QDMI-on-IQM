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

"""Shared fixtures for QDMI use-case workflow tests."""

from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from mqt.core.plugins.qiskit.provider import QDMIProvider

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

support_module = importlib.import_module("support")
ShowcaseBackend = support_module.ShowcaseBackend
require_showcase_backend_access = support_module.require_showcase_backend_access
showcase_backend_kind = support_module.showcase_backend_kind


@pytest.fixture(scope="session")
def backend() -> QDMIBackend:
    """Return the configured showcase backend for use-case workflows.

    Raises:
        ValueError: If the showcase backend selection is unsupported.
    """
    require_showcase_backend_access()

    if showcase_backend_kind() is ShowcaseBackend.IQM:
        return IQMBackend(
            base_url=os.getenv("IQM_BASE_URL"),
            token=os.getenv("IQM_TOKEN") or os.getenv("RESONANCE_API_KEY"),
            tokens_file=os.getenv("IQM_TOKENS_FILE"),
            qc_id=os.getenv("IQM_QC_ID"),
            qc_alias=os.getenv("IQM_QC_ALIAS"),
        )

    provider = QDMIProvider()
    try:
        return provider.get_backend("MQT Core DDSIM QDMI Device")
    except ValueError:
        ddsim_backends = provider.backends(name="DDSIM")
        if len(ddsim_backends) == 1:
            return ddsim_backends[0]

        if not ddsim_backends:
            msg = "No DDSIM QDMI backend is available through QDMIProvider()."
            raise ValueError(msg) from None

        backend_names = ", ".join(backend.name or "<unnamed>" for backend in ddsim_backends)
        msg = f"Multiple DDSIM backends matched the showcase selection: {backend_names}."
        raise ValueError(msg) from None


@pytest.fixture(scope="session")
def target_label(backend: QDMIBackend) -> str:
    """Return a human-readable label for the selected target."""
    if showcase_backend_kind() is ShowcaseBackend.IQM:
        return os.getenv("IQM_QC_ALIAS") or os.getenv("IQM_QC_ID") or os.getenv("IQM_BASE_URL") or "default-target"

    return backend.name or "MQT Core DDSIM QDMI Device"
