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

import pytest

from iqm.qdmi.qiskit import IQMBackend

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

support_module = importlib.import_module("support")
require_iqm_access = support_module.require_iqm_access


@pytest.fixture(scope="session")
def backend() -> IQMBackend:
    """Return the configured IQM backend for use-case workflows."""
    require_iqm_access()
    return IQMBackend(
        base_url=os.getenv("IQM_BASE_URL"),
        token=os.getenv("IQM_TOKEN") or os.getenv("RESONANCE_API_KEY"),
        tokens_file=os.getenv("IQM_TOKENS_FILE"),
        qc_id=os.getenv("IQM_QC_ID"),
        qc_alias=os.getenv("IQM_QC_ALIAS"),
    )


@pytest.fixture(scope="session")
def target_label() -> str:
    """Return a human-readable label for the selected target."""
    return os.getenv("IQM_QC_ALIAS") or os.getenv("IQM_QC_ID") or os.getenv("IQM_BASE_URL") or "default-target"
