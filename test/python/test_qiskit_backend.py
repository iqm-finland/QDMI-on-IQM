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
from pathlib import Path
from typing import Any

import pytest
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile
from qiskit.quantum_info import SparsePauliOp

from iqm.qdmi import qiskit as iqm_qiskit
from iqm.qdmi.qiskit import IQMBackend

ENVIRONMENT_TOKENS_FILE = Path("/opt/iqm/environment-tokens.json")
EXPLICIT_TOKENS_FILE = Path("/opt/iqm/explicit-tokens.json")


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
            library_path: str | os.PathLike[str],
            prefix: str,
            *,
            base_url: str | None = None,
        ) -> None:
            captured["definition"] = self
            captured["definition_kwargs"] = {
                "device_id": device_id,
                "library_path": library_path,
                "prefix": prefix,
                "base_url": base_url,
            }

    def fake_register_device_if_absent(definition: object) -> bool:
        captured["registered"] = definition
        return True

    def fake_open_device(device_id: str, **session: str | Path | None) -> object:
        captured["opened_id"] = device_id
        captured["session"] = session
        return fake_device

    def fake_qdmi_backend_init(_self: IQMBackend, device: object) -> None:
        captured["device"] = device

    monkeypatch.setattr(iqm_qiskit, "DeviceDefinition", FakeDeviceDefinition)
    monkeypatch.setattr(iqm_qiskit, "register_device_if_absent", fake_register_device_if_absent)
    monkeypatch.setattr(iqm_qiskit, "open_device", fake_open_device)
    monkeypatch.setattr(iqm_qiskit.QDMIBackend, "__init__", fake_qdmi_backend_init)
    return captured


def _expected_definition() -> dict[str, str | os.PathLike[str]]:
    return {
        "device_id": iqm_qiskit.IQM_QDMI_DEVICE_ID,
        "library_path": iqm_qiskit.IQM_QDMI_LIBRARY_PATH,
        "prefix": iqm_qiskit.IQM_QDMI_PREFIX,
        "base_url": "https://resonance.iqm.tech",
    }


def test_iqm_backend_uses_environment_defaults(monkeypatch: pytest.MonkeyPatch) -> None:
    """The backend should forward the canonical IQM environment variables."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_BASE_URL", "https://environment.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", str(ENVIRONMENT_TOKENS_FILE))
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QC_ALIAS", "environment-qc-alias")
    environment_token = "environment-token"  # ruff:ignore[hardcoded-password-string]

    IQMBackend()

    assert captured["device"] is not None
    assert captured["registered"] is captured["definition"]
    assert captured["definition_kwargs"] == _expected_definition()
    assert captured["opened_id"] == iqm_qiskit.IQM_QDMI_DEVICE_ID
    assert captured["session"] == {
        "base_url": "https://environment.example",
        "token": environment_token,
        "auth_file": ENVIRONMENT_TOKENS_FILE,
        "custom1": "environment-qc-id",
        "custom2": "environment-qc-alias",
    }


def test_iqm_backend_prefers_explicit_arguments_over_environment(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Explicit backend arguments should override inherited environment values."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_BASE_URL", "https://environment.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", str(ENVIRONMENT_TOKENS_FILE))
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QC_ALIAS", "environment-qc-alias")
    explicit_token = "explicit-token"  # ruff:ignore[hardcoded-password-string]

    IQMBackend(
        base_url="https://explicit.example",
        token=explicit_token,
        tokens_file=EXPLICIT_TOKENS_FILE,
        qc_id="explicit-qc-id",
        qc_alias="explicit-qc-alias",
    )

    assert captured["session"] == {
        "base_url": "https://explicit.example",
        "token": explicit_token,
        "auth_file": EXPLICIT_TOKENS_FILE,
        "custom1": "explicit-qc-id",
        "custom2": "explicit-qc-alias",
    }


def test_iqm_backend_preserves_existing_registration(monkeypatch: pytest.MonkeyPatch) -> None:
    """An existing configured definition should win over the packaged fallback."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.delenv("IQM_BASE_URL", raising=False)

    def existing_registration(_definition: object) -> bool:
        return False

    monkeypatch.setattr(iqm_qiskit, "register_device_if_absent", existing_registration)

    IQMBackend()

    assert captured["opened_id"] == iqm_qiskit.IQM_QDMI_DEVICE_ID
    assert captured["session"]["base_url"] is None


@pytest.mark.parametrize(
    ("base_url", "environment_base_url"),
    [
        pytest.param(None, "", id="empty-environment"),
        pytest.param("", None, id="empty-explicit"),
    ],
)
def test_iqm_backend_treats_empty_base_url_as_unset(
    monkeypatch: pytest.MonkeyPatch,
    base_url: str | None,
    environment_base_url: str | None,
) -> None:
    """An empty endpoint should not override the registered device default."""
    captured = _stub_backend_construction(monkeypatch)
    if environment_base_url is None:
        monkeypatch.delenv("IQM_BASE_URL", raising=False)
    else:
        monkeypatch.setenv("IQM_BASE_URL", environment_base_url)

    IQMBackend(base_url=base_url)

    assert captured["definition_kwargs"] == _expected_definition()
    assert captured["session"]["base_url"] is None


def test_iqm_backend_propagates_disabled_device(monkeypatch: pytest.MonkeyPatch) -> None:
    """A disabled stable ID should not be re-enabled by the packaged fallback."""
    _stub_backend_construction(monkeypatch)

    def disabled_registration(_definition: object) -> bool:
        return False

    def disabled_open(_device_id: str, **_session: str | Path | None) -> object:
        msg = "QDMI device ID 'iqm.default' is disabled by configuration"
        raise RuntimeError(msg)

    monkeypatch.setattr(iqm_qiskit, "register_device_if_absent", disabled_registration)
    monkeypatch.setattr(iqm_qiskit, "open_device", disabled_open)

    with pytest.raises(RuntimeError, match="disabled by configuration"):
        IQMBackend()


def test_iqm_backend_propagates_invalid_registration(monkeypatch: pytest.MonkeyPatch) -> None:
    """Invalid packaged definitions should not be mistaken for existing IDs."""
    _stub_backend_construction(monkeypatch)

    def invalid_registration(_definition: object) -> bool:
        msg = "invalid device definition"
        raise ValueError(msg)

    monkeypatch.setattr(iqm_qiskit, "register_device_if_absent", invalid_registration)

    with pytest.raises(ValueError, match="invalid device definition"):
        IQMBackend()


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
    job = backend.estimator(default_shots=32).run([(transpiled_circuit, observable)])
    result = job.result()[0]
    expectation_value = float(result.data["evs"][()])
    standard_deviation = float(result.data["stds"][()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    assert result.metadata["shots"] == 32
