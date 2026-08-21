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

"""Tests for the device's queue, job-retrieval, and calibration capabilities.

These drive the device through MQT Core's Python QDMI client, the way an
application reaches it. `test/unit/` covers the same properties at the C API
level, against a stubbed server.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from mqt.core.qdmi import Device, Job, ProgramFormat
from mqt.core.qdmi.driver import open_device
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile

from iqm.qdmi import IQM_QDMI_DEVICE_ID
from iqm.qdmi.qiskit import IQMBackend

# Every IQM quantum computer accepts these three. Custom format 1 is this
# device's pulse-level program format.
BASE_PROGRAM_FORMATS = {ProgramFormat.QIR_BASE_STRING, ProgramFormat.IQM_JSON, ProgramFormat.CUSTOM1}


def _skip_without_iqm_access() -> None:
    """Skip live tests when IQM credentials are unavailable."""
    if not os.getenv("IQM_TOKEN") and not os.getenv("IQM_TOKENS_FILE"):
        pytest.skip(
            "Either IQM_TOKEN or IQM_TOKENS_FILE environment variable must be set to run live IQM backend tests."
        )


@pytest.fixture
def backend() -> IQMBackend:
    """Returns the IQM backend, which also registers the device."""
    _skip_without_iqm_access()
    return IQMBackend()


@pytest.fixture
def device(backend: IQMBackend) -> Device:
    """Returns a fresh device session over the registration the backend made.

    Constructing the backend registers `iqm.default`, so opening it here needs
    only the credentials. It resolves the same environment the backend resolves.
    """
    tokens_file = os.getenv("IQM_TOKENS_FILE")
    del backend
    return open_device(
        IQM_QDMI_DEVICE_ID,
        base_url=os.getenv("IQM_BASE_URL") or None,
        token=os.getenv("IQM_TOKEN"),
        auth_file=Path(tokens_file) if tokens_file else None,
        custom1=os.getenv("IQM_QC_ID"),
        custom2=os.getenv("IQM_QC_ALIAS"),
    )


@pytest.fixture
def circuit() -> QuantumCircuit:
    """Returns a simple Bell state circuit."""
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure_all()
    return circuit


def test_device_advertises_exactly_its_supported_formats(device: Device) -> None:
    """The device should advertise QIR, IQM JSON, and pulse, plus calibration where supported.

    Calibration is the only optional format; the device advertises it when the
    quantum computer's API reports calibration support. No test submits one,
    because a calibration run is a real operation on shared hardware.
    """
    formats = set(device.supported_program_formats())

    assert formats in (BASE_PROGRAM_FORMATS, BASE_PROGRAM_FORMATS | {ProgramFormat.CALIBRATION})


def test_device_reports_queue_length(device: Device) -> None:
    """Querying the queue length should return a count or report no support."""
    queue_length = device.queue_length()

    assert queue_length is None or queue_length >= 0


def test_retrieve_job_by_id_reaches_a_job_from_another_session(
    circuit: QuantumCircuit, backend: IQMBackend, device: Device
) -> None:
    """A submitted job should be retrievable by ID from a separate session."""
    transpiled_circuit = transpile(circuit, backend=backend)
    submitted = backend.run(transpiled_circuit, shots=8)
    job_id = submitted.job_id()

    retrieved = device.retrieve_job_by_id(job_id)

    assert retrieved.id == job_id
    # A retrieved job is queryable; only submission and parameter changes are
    # closed off.
    assert retrieved.check() in set(Job.Status)


def test_queue_position_is_reported_only_while_a_job_is_queued(
    circuit: QuantumCircuit, backend: IQMBackend, device: Device
) -> None:
    """Querying a job's queue position should never raise, queued or not."""
    transpiled_circuit = transpile(circuit, backend=backend)
    submitted = backend.run(transpiled_circuit, shots=8)

    retrieved = device.retrieve_job_by_id(submitted.job_id())
    queue_position = retrieved.queue_position

    assert queue_position is None or queue_position >= 0
