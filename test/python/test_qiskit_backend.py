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

# pyright: reportArgumentType=false, reportUnknownArgumentType=false, reportUnknownMemberType=false, reportUnknownVariableType=false, reportMissingImports=false, reportMissingModuleSource=false, reportMissingTypeStubs=false

"""Tests for the Qiskit-facing IQM backend wrapper."""

from __future__ import annotations

import os
from typing import Any

import pytest
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile
from qiskit.quantum_info import SparsePauliOp

from iqm.qdmi import qiskit as iqm_qiskit
from iqm.qdmi.qiskit import IQMBackend

ENVIRONMENT_TOKENS_FILE = "/opt/iqm/environment-tokens.json"
EXPLICIT_TOKENS_FILE = "/opt/iqm/explicit-tokens.json"


def _stub_backend_construction(monkeypatch: pytest.MonkeyPatch) -> dict[str, Any]:
    """Stub backend construction so environment-resolution tests stay hermetic.

    Returns:
        Captured constructor state from the fake device registry and backend base class.
    """
    captured: dict[str, Any] = {}
    fake_device = object()

    class FakeDeviceDefinition:
        def __init__(
            self,
            device_id: str,
            *,
            library_path: str,
            prefix: str,
            base_url: str,
            token: str | None,
            auth_file: str | None,
            custom1: str | None,
            custom2: str | None,
        ) -> None:
            captured["definition"] = self
            captured["kwargs"] = {
                "device_id": device_id,
                "library_path": library_path,
                "prefix": prefix,
                "base_url": base_url,
                "token": token,
                "auth_file": auth_file,
                "custom1": custom1,
                "custom2": custom2,
            }

    def fake_register_device(definition: object) -> None:
        captured["registered"] = definition

    def fake_open_device(device_id: str) -> object:
        captured["opened_id"] = device_id
        return fake_device

    def fake_device_id() -> str:
        return "test-device-id"

    def fake_qdmi_backend_init(_self: IQMBackend, device: object) -> None:
        captured["device"] = device

    monkeypatch.setattr(iqm_qiskit, "DeviceDefinition", FakeDeviceDefinition)
    monkeypatch.setattr(iqm_qiskit, "register_device", fake_register_device)
    monkeypatch.setattr(iqm_qiskit, "open_device", fake_open_device)
    monkeypatch.setattr(iqm_qiskit, "uuid4", fake_device_id)
    monkeypatch.setattr(iqm_qiskit.QDMIBackend, "__init__", fake_qdmi_backend_init)
    return captured


def _expected_registration(**session: str | None) -> dict[str, str | None]:
    return {
        "device_id": "iqm.runtime.test-device-id",
        "library_path": str(iqm_qiskit.IQM_QDMI_LIBRARY_PATH),
        "prefix": "IQM",
        **session,
    }


def test_iqm_backend_uses_environment_defaults(monkeypatch: pytest.MonkeyPatch) -> None:
    """The backend should forward the canonical IQM environment variables."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_BASE_URL", "https://environment.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", ENVIRONMENT_TOKENS_FILE)
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QC_ALIAS", "environment-qc-alias")
    environment_token = "environment-token"  # noqa: S105

    IQMBackend()

    assert captured["device"] is not None
    assert captured["registered"] is captured["definition"]
    assert captured["opened_id"] == "iqm.runtime.test-device-id"
    assert captured["kwargs"] == _expected_registration(
        base_url="https://environment.example",
        token=environment_token,
        auth_file=ENVIRONMENT_TOKENS_FILE,
        custom1="environment-qc-id",
        custom2="environment-qc-alias",
    )


def test_iqm_backend_prefers_explicit_arguments_over_environment(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Explicit backend arguments should override inherited environment values."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_BASE_URL", "https://environment.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", ENVIRONMENT_TOKENS_FILE)
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QC_ALIAS", "environment-qc-alias")
    explicit_token = "explicit-token"  # noqa: S105

    IQMBackend(
        base_url="https://explicit.example",
        token=explicit_token,
        tokens_file=EXPLICIT_TOKENS_FILE,
        qc_id="explicit-qc-id",
        qc_alias="explicit-qc-alias",
    )

    assert captured["kwargs"] == _expected_registration(
        base_url="https://explicit.example",
        token=explicit_token,
        auth_file=EXPLICIT_TOKENS_FILE,
        custom1="explicit-qc-id",
        custom2="explicit-qc-alias",
    )


def test_iqm_backend_does_not_use_legacy_resonance_api_key(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Legacy RESONANCE_API_KEY should not be consulted at runtime anymore."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.delenv("IQM_BASE_URL", raising=False)
    monkeypatch.delenv("IQM_TOKEN", raising=False)
    monkeypatch.delenv("IQM_TOKENS_FILE", raising=False)
    monkeypatch.delenv("IQM_QC_ID", raising=False)
    monkeypatch.delenv("IQM_QC_ALIAS", raising=False)
    monkeypatch.setenv("RESONANCE_API_KEY", "legacy-token")

    IQMBackend()

    assert captured["kwargs"] == _expected_registration(
        base_url="https://resonance.iqm.tech",
        token=None,
        auth_file=None,
        custom1=None,
        custom2=None,
    )


def _skip_without_iqm_access() -> None:
    """Skip live tests when IQM credentials are unavailable."""
    if not os.getenv("IQM_TOKEN") and not os.getenv("IQM_TOKENS_FILE"):
        pytest.skip(
            "Either IQM_TOKEN or IQM_TOKENS_FILE environment variable must be set to run live IQM backend tests."
        )


@pytest.fixture
def backend() -> IQMBackend:
    """Returns the IQM backend."""
    _skip_without_iqm_access()
    return IQMBackend()


@pytest.fixture
def circuit() -> QuantumCircuit:
    """Returns a simple Bell state circuit."""
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    return circuit


def test_iqm_backend(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """Test the execution of a simple Bell state circuit."""
    circuit.measure_all()
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.run(transpiled_circuit, shots=8)
    counts = job.result().get_counts()
    assert sum(counts.values()) == 8


def test_iqm_backend_sampler(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """The bound sampler should execute a simple circuit on the live IQM backend."""
    circuit.measure_all()
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.sampler().run([(transpiled_circuit,)], shots=8)
    counts = job.result()[0].data["meas"].get_counts()
    assert sum(counts.values()) == 8


def test_iqm_backend_estimator(circuit: QuantumCircuit, backend: IQMBackend) -> None:
    """The bound estimator should execute a simple observable on the live IQM backend."""
    observable = SparsePauliOp("Z" * backend.num_qubits)
    transpiled_circuit = transpile(circuit, backend=backend)
    job = backend.estimator(options={"default_shots": 32}).run([(transpiled_circuit, observable)])
    result = job.result()[0]
    expectation_value = float(result.data["evs"][()])
    standard_deviation = float(result.data["stds"][()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    assert result.metadata["shots"] == 32
