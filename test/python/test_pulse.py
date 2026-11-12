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

"""Binary pulse transport through MQT Core's native QDMI client."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest
from mqt.core.qdmi import CustomProperty, Job, ProgramFormat

if TYPE_CHECKING:
    from iqm.qdmi.qiskit import IQMBackend


@pytest.mark.parametrize("offline_backend", ["pulse"], indirect=True)
@pytest.mark.parametrize("payload", [b"\x00\xff\x00", bytes(range(256)) * 4096], ids=["binary", "one-mib"])
def test_pulse_binary_round_trip(offline_backend: IQMBackend, offline_submissions: list[dict], payload: bytes) -> None:
    """Preserve binary programs and cached artifacts, including retrieved jobs."""
    device = offline_backend.device
    job = device.submit_job(payload, ProgramFormat.CUSTOM1)
    assert offline_submissions == [{"pulse": payload}]
    assert job.check() == Job.Status.DONE
    artifact = b"\x08\x00\xff\x00"
    assert job.get_custom_result(CustomProperty.CUSTOM2, bytes) == artifact
    assert job.get_custom_result(CustomProperty.CUSTOM2, bytes) == artifact
    retrieved = device.retrieve_job_by_id(job.id)
    assert retrieved.get_custom_result(CustomProperty.CUSTOM2, bytes) == artifact
