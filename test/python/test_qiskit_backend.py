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

    def fake_open_device(device_id: str, **session: str | Path | None) -> object:
        captured["opened_id"] = device_id
        captured["session"] = session
        return fake_device

    def fake_qdmi_backend_init(_self: IQMBackend, device: object, **metadata: object) -> None:
        captured["device"] = device
        captured["metadata"] = metadata

    monkeypatch.setattr(iqm_qiskit, "open_device", fake_open_device)
    monkeypatch.setattr("mqt.core.plugins.qiskit.backend.open_device", fake_open_device)
    monkeypatch.setattr(iqm_qiskit.QDMIBackend, "__init__", fake_qdmi_backend_init)
    return captured


def test_iqm_backend_uses_environment_defaults(monkeypatch: pytest.MonkeyPatch) -> None:
    """The backend should forward the canonical IQM environment variables."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_SERVER_URL", "https://canonical.example")
    monkeypatch.setenv("IQM_BASE_URL", "https://legacy.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", str(ENVIRONMENT_TOKENS_FILE))
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", "canonical-qc-alias")
    monkeypatch.setenv("IQM_QC_ALIAS", "legacy-qc-alias")
    environment_token = "environment-token"  # ruff:ignore[hardcoded-password-string]

    IQMBackend()

    assert captured["device"] is not None
    assert captured["opened_id"] == iqm_qiskit.IQM_QDMI_DEVICE_ID
    assert captured["session"] == {
        "base_url": "https://canonical.example",
        "token": environment_token,
        "auth_file": ENVIRONMENT_TOKENS_FILE,
        "custom1": "environment-qc-id",
        "custom2": "canonical-qc-alias",
    }


def test_iqm_backend_supports_legacy_environment_aliases(monkeypatch: pytest.MonkeyPatch) -> None:
    """The former routing variables should remain supported aliases."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.delenv("IQM_SERVER_URL", raising=False)
    monkeypatch.delenv("IQM_QUANTUM_COMPUTER", raising=False)
    monkeypatch.setenv("IQM_BASE_URL", "https://legacy.example")
    monkeypatch.setenv("IQM_QC_ALIAS", "legacy-qc-alias")

    IQMBackend()

    assert captured["session"]["base_url"] == "https://legacy.example"
    assert captured["session"]["custom2"] == "legacy-qc-alias"


def test_iqm_backend_prefers_explicit_arguments_over_environment(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Explicit backend arguments should override inherited environment values."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_SERVER_URL", "https://canonical.example")
    monkeypatch.setenv("IQM_BASE_URL", "https://legacy.example")
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    monkeypatch.setenv("IQM_TOKENS_FILE", str(ENVIRONMENT_TOKENS_FILE))
    monkeypatch.setenv("IQM_QC_ID", "environment-qc-id")
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", "canonical-qc-alias")
    monkeypatch.setenv("IQM_QC_ALIAS", "legacy-qc-alias")
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
    monkeypatch.delenv("IQM_SERVER_URL", raising=False)
    monkeypatch.delenv("IQM_BASE_URL", raising=False)

    IQMBackend()

    assert captured["opened_id"] == iqm_qiskit.IQM_QDMI_DEVICE_ID
    assert captured["session"]["base_url"] is None


@pytest.mark.parametrize("device_id", ["iqm.default", "iqm.garnet.mock"])
def test_iqm_backend_explicit_selection_ignores_environment(monkeypatch: pytest.MonkeyPatch, device_id: str) -> None:
    """A named preset or explicit alias must not inherit another device's ID."""
    captured = _stub_backend_construction(monkeypatch)
    monkeypatch.setenv("IQM_QC_ID", "other-device")
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", "other-alias")
    monkeypatch.setenv("IQM_SERVER_URL", "https://other.example")

    IQMBackend(device_id, qc_alias="garnet:mock" if device_id == "iqm.default" else None)

    assert captured["opened_id"] == device_id
    assert captured["session"]["custom1"] is None
    assert captured["session"]["custom2"] == ("garnet:mock" if device_id == "iqm.default" else None)
    if device_id != "iqm.default":
        assert captured["session"]["base_url"] is None


def test_iqm_backend_from_device_id(monkeypatch: pytest.MonkeyPatch) -> None:
    """Core's factory retains the IQM adapter and the selected stable ID."""
    captured = _stub_backend_construction(monkeypatch)

    backend = IQMBackend.from_device_id("iqm.emerald.mock")

    assert isinstance(backend, IQMBackend)
    assert captured["opened_id"] == "iqm.emerald.mock"
    assert captured["metadata"]["device_id"] == "iqm.emerald.mock"
    with pytest.raises(ValueError, match="already-open device"):
        IQMBackend(device=captured["device"], qc_alias="garnet")


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
    """An empty endpoint should not override the manifest default."""
    captured = _stub_backend_construction(monkeypatch)
    if environment_base_url is None:
        monkeypatch.delenv("IQM_SERVER_URL", raising=False)
    else:
        monkeypatch.setenv("IQM_SERVER_URL", environment_base_url)
    monkeypatch.delenv("IQM_BASE_URL", raising=False)

    IQMBackend(base_url=base_url)

    assert captured["session"]["base_url"] is None


def test_iqm_backend_propagates_disabled_device(monkeypatch: pytest.MonkeyPatch) -> None:
    """A disabled stable ID should not be re-enabled by the packaged fallback."""
    _stub_backend_construction(monkeypatch)

    def disabled_open(_device_id: str, **_session: str | Path | None) -> object:
        msg = "QDMI device ID 'iqm.default' is disabled by configuration"
        raise RuntimeError(msg)

    monkeypatch.setattr(iqm_qiskit, "open_device", disabled_open)

    with pytest.raises(RuntimeError, match="disabled by configuration"):
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
    job = backend.estimator(default_precision=1 / 8).run([(transpiled_circuit, observable)])
    result = job.result()[0]
    expectation_value = float(result.data["evs"][()])
    standard_deviation = float(result.data["stds"][()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    assert result.metadata["shots"] == 64
