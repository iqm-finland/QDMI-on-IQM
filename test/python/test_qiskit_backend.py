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

import json
import os
from pathlib import Path
from typing import Any
from unittest.mock import Mock

import numpy as np
import pytest
from mqt.core.plugins.qiskit.backend import QDMIBackend
from mqt.core.qdmi import Device, Job, ProgramFormat
from mqt.core.qdmi.driver import open_device
from qiskit.circuit import ClassicalRegister, Parameter, QuantumCircuit, QuantumRegister
from qiskit.compiler import transpile
from qiskit.quantum_info import SparsePauliOp

from iqm.qdmi import qiskit as iqm_qiskit
from iqm.qdmi._backends import build_estimator  # ruff:ignore[import-private-name]
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
    assert captured["registered"] is captured["definition"]
    assert captured["definition_kwargs"] == _expected_definition()
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
        monkeypatch.delenv("IQM_SERVER_URL", raising=False)
    else:
        monkeypatch.setenv("IQM_SERVER_URL", environment_base_url)
    monkeypatch.delenv("IQM_BASE_URL", raising=False)

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
    job = backend.estimator(default_precision=1 / 8).run([(transpiled_circuit, observable)])
    result = job.result()[0]
    expectation_value = float(result.data["evs"][()])
    standard_deviation = float(result.data["stds"][()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    assert result.metadata["shots"] == 64


@pytest.fixture
def iqm_results(monkeypatch: pytest.MonkeyPatch) -> tuple[IQMBackend, Mock, Mock]:
    """Return an IQM adapter with simulator topology and stubbed remote results."""
    device = open_device("mqt.ddsim.default")
    monkeypatch.setattr(iqm_qiskit, "register_device_if_absent", Mock())
    monkeypatch.setattr(iqm_qiskit, "open_device", Mock(return_value=device))
    monkeypatch.setattr(Device, "supported_program_formats", Mock(return_value=[ProgramFormat.IQM_JSON]))
    job = Mock(spec=Job)
    job.id = "offline-job"
    job.check.return_value = Job.Status.DONE
    submit = Mock(return_value=job)
    monkeypatch.setattr(Device, "submit_job", submit)
    return IQMBackend(), job, submit


def test_iqm_primitives_preserve_ordered_shots(iqm_results: tuple[IQMBackend, Mock, Mock]) -> None:
    """Native memory and sampler results preserve shot order across registers."""
    backend, job, submit = iqm_results
    circuit = QuantumCircuit(QuantumRegister(3), ClassicalRegister(1, "left"), ClassicalRegister(2, "right"))
    circuit.measure(range(3), range(3))
    job.get_shots.return_value = ["101", "000", "110", "101"]

    result = backend.run(circuit, shots=4, memory=True).result()
    assert result.get_memory() == ["10 1", "00 0", "11 0", "10 1"]
    assert result.get_counts() == {"10 1": 2, "00 0": 1, "11 0": 1}

    data = backend.sampler(default_shots=4).run([circuit]).result()[0].data
    assert data["left"].get_bitstrings() == ["1", "0", "0", "1"]
    assert data["right"].get_bitstrings() == ["10", "00", "11", "10"]
    job.get_counts.assert_not_called()
    assert submit.call_count == 2
    assert submit.call_args.kwargs["program_format"] == ProgramFormat.IQM_JSON
    program = json.loads(submit.call_args.kwargs["program"])
    assert [op["args"]["key"] for op in program["instructions"]] == ["left_1_0_0", "right_2_1_0", "right_2_1_1"]


def test_iqm_counts_only_support_estimator(iqm_results: tuple[IQMBackend, Mock, Mock]) -> None:
    """Counts-only jobs support estimation but fail when a sampler collects shots."""
    backend, job, submit = iqm_results
    job.get_shots.side_effect = RuntimeError("SHOTS unavailable")
    circuit = QuantumCircuit(1)
    circuit.measure_all()
    with pytest.raises(RuntimeError, match="SHOTS unavailable"):
        backend.sampler(default_shots=4).run([circuit]).result()

    job.get_shots.reset_mock()
    job.get_counts.return_value = {"0": 4096}
    result = backend.estimator().run([(QuantumCircuit(1), SparsePauliOp("Z"))]).result()[0]
    assert result.data["evs"] == pytest.approx(1)
    assert result.data["stds"] == pytest.approx(0)
    assert result.metadata["shots"] == 4096
    assert submit.call_args.kwargs["num_shots"] == 4096
    job.get_shots.assert_not_called()


def test_estimator_groups_observables_and_broadcasts(monkeypatch: pytest.MonkeyPatch) -> None:
    """The shared estimator groups compatible terms across parameter bindings."""
    estimator = build_estimator(simulator=True)
    assert isinstance(estimator.backend, QDMIBackend)
    submit = Mock(wraps=estimator.backend.device.submit_job)
    monkeypatch.setattr(Device, "submit_job", submit)
    theta = Parameter("theta")
    circuit = QuantumCircuit(2)
    circuit.ry(theta, 0)
    observable = SparsePauliOp.from_list([("IZ", 1.0), ("ZI", 2.0)])  # spellchecker:disable-line

    result = estimator.run([(circuit, observable, {theta: [[0], [np.pi]]})], precision=1 / 8).result()[0]

    np.testing.assert_allclose(result.data["evs"], [3, 1])
    np.testing.assert_allclose(result.data["stds"], [0, 0])
    assert result.metadata["shots"] == 64
    assert submit.call_count == 2  # One grouped measurement circuit per parameter binding.
