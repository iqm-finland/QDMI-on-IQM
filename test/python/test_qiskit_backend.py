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

# pyright: reportArgumentType=false, reportUnknownArgumentType=false, reportUnknownMemberType=false, reportUnknownVariableType=false, reportMissingImports=false, reportMissingModuleSource=false, reportMissingTypeStubs=false

"""Tests for the Qiskit-facing IQM backend wrapper."""

from __future__ import annotations

import os
from importlib import import_module
from math import pi
from typing import TYPE_CHECKING, Protocol, cast

import pytest
from mqt.core.plugins.qiskit import QDMIEstimator, QDMISampler

from iqm import qdmi

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path

pytestmark = [
    pytest.mark.filterwarnings("ignore:.*Device operation.*cannot be mapped to a Qiskit gate.*:UserWarning"),
    pytest.mark.filterwarnings("ignore:Device does not define a measurement operation.*:UserWarning"),
]


class _CircuitLike(Protocol):
    def r(self, theta: float, phi: float, qubit: int) -> object:
        pass

    def measure_all(self) -> None:
        pass


class _BitArrayLike(Protocol):
    def get_counts(self) -> dict[str, int]:
        pass


class _SamplerDataLike(Protocol):
    meas: _BitArrayLike


class _ScalarLike(Protocol):
    def __getitem__(self, key: tuple[()]) -> float:
        pass


class _EstimatorDataLike(Protocol):
    evs: _ScalarLike
    stds: _ScalarLike


class _CountsResultLike(Protocol):
    def get_counts(self) -> dict[str, int]:
        pass


def _load_quantum_circuit() -> Callable[[int], _CircuitLike]:
    """Return the Qiskit QuantumCircuit constructor."""
    qiskit_module = import_module("qiskit")
    return cast("Callable[[int], _CircuitLike]", qiskit_module.QuantumCircuit)


def _load_sparse_pauli_op() -> Callable[[str], object]:
    """Return the Qiskit SparsePauliOp constructor."""
    quantum_info_module = import_module("qiskit.quantum_info")
    return cast("Callable[[str], object]", quantum_info_module.SparsePauliOp)


def _patch_backend_initialization(
    monkeypatch: pytest.MonkeyPatch,
) -> tuple[dict[str, str | None], dict[str, object], object]:
    """Patch dynamic device loading and backend initialization for unit tests.

    Returns:
        The captured device-library kwargs, captured backend-init state, and fake device.
    """
    captured: dict[str, str | None] = {}
    backend_state: dict[str, object] = {}
    fake_device = object()

    def fake_add_dynamic_device_library(**kwargs: str | None) -> object:
        captured.update(kwargs)
        return fake_device

    def fake_backend_init(self: object, device: object, provider: object | None = None) -> None:
        del provider
        del self
        backend_state["device"] = device

    monkeypatch.setattr("iqm.qdmi.qiskit.fomac.add_dynamic_device_library", fake_add_dynamic_device_library)
    monkeypatch.setattr("iqm.qdmi.qiskit.QDMIBackend.__init__", fake_backend_init)
    return captured, backend_state, fake_device


def _skip_without_iqm_access() -> None:
    """Skip live tests when IQM credentials are unavailable."""
    if not os.getenv("RESONANCE_API_KEY"):
        pytest.skip("RESONANCE_API_KEY is required for live IQM backend tests")


def test_iqm_backend_forwards_configuration(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """IQMBackend should forward configuration to the packaged device library."""
    captured, backend_state, fake_device = _patch_backend_initialization(monkeypatch)
    auth_file = tmp_path / "auth.json"
    auth_token = os.environ.get("IQM_TEST_TOKEN", "test-token")
    test_password = os.environ.get("IQM_TEST_PASSWORD", "test-password")

    backend = qdmi.IQMBackend(
        base_url="https://example.invalid",
        token=auth_token,
        auth_file=str(auth_file),
        auth_url="https://auth.example.invalid",
        username="user",
        password=test_password,
        qc_id="garnet",
        qc_alias="garnet:mock",
    )

    assert captured == {
        "library_path": str(qdmi.IQM_QDMI_LIBRARY_PATH),
        "prefix": "IQM",
        "base_url": "https://example.invalid",
        "token": auth_token,
        "auth_file": str(auth_file),
        "auth_url": "https://auth.example.invalid",
        "username": "user",
        "password": test_password,
        "custom1": "garnet",
        "custom2": "garnet:mock",
    }
    assert backend_state["device"] is fake_device
    assert isinstance(backend, qdmi.IQMBackend)


def test_iqm_backend_uses_environment_defaults(monkeypatch: pytest.MonkeyPatch) -> None:
    """IQMBackend should fall back to the established IQM environment variables."""
    captured, backend_state, fake_device = _patch_backend_initialization(monkeypatch)
    env_token = os.environ.get("IQM_TEST_ENV_TOKEN", "env-token")
    monkeypatch.setenv("IQM_BASE_URL", "https://resonance.test")
    monkeypatch.setenv("RESONANCE_API_KEY", env_token)
    monkeypatch.setenv("IQM_QC_ID", "env-qc-id")
    monkeypatch.setenv("IQM_QC_ALIAS", "env:qpu")

    backend = qdmi.IQMBackend()

    assert captured["base_url"] == "https://resonance.test"
    assert captured["token"] == env_token
    assert captured["custom1"] == "env-qc-id"
    assert captured["custom2"] == "env:qpu"
    assert backend_state["device"] is fake_device
    assert isinstance(backend, qdmi.IQMBackend)


def test_iqm_backend_creates_bound_primitives(monkeypatch: pytest.MonkeyPatch) -> None:
    """Sampler and estimator helpers should return primitives bound to the backend."""
    _patch_backend_initialization(monkeypatch)
    backend = qdmi.IQMBackend(base_url="https://example.invalid")

    sampler = backend.sampler(default_shots=256, options={"tag": "sampler"})
    estimator = backend.estimator(default_precision=0.25, options={"default_shots": 64})

    assert isinstance(sampler, QDMISampler)
    assert sampler.backend is backend
    assert isinstance(estimator, QDMIEstimator)
    assert estimator.backend is backend


@pytest.mark.integration
def test_iqm_backend_live_run() -> None:
    """A simple Qiskit circuit should execute through the live IQM backend."""
    _skip_without_iqm_access()

    backend = qdmi.IQMBackend()
    circuit = _load_quantum_circuit()(1)
    circuit.r(pi / 2, 0.0, 0)
    circuit.measure_all()

    job = backend.run(circuit, shots=8)  # ty: ignore
    result = cast("_CountsResultLike", job.result())

    counts = result.get_counts()
    assert sum(counts.values()) == 8


@pytest.mark.integration
def test_iqm_backend_live_sampler() -> None:
    """The bound sampler should execute a simple circuit on the live IQM backend."""
    _skip_without_iqm_access()

    backend = qdmi.IQMBackend()
    circuit = _load_quantum_circuit()(1)
    circuit.r(pi / 2, 0.0, 0)
    circuit.measure_all()

    result = backend.sampler().run([(circuit,)], shots=8).result()[0]  # ty: ignore
    sampler_data = cast("_SamplerDataLike", result.data)
    counts = sampler_data.meas.get_counts()

    assert sum(counts.values()) == 8


@pytest.mark.integration
def test_iqm_backend_live_estimator() -> None:
    """The bound estimator should execute a simple observable on the live IQM backend."""
    _skip_without_iqm_access()

    backend = qdmi.IQMBackend()
    circuit = _load_quantum_circuit()(1)
    circuit.r(pi / 2, 0.0, 0)
    observable = _load_sparse_pauli_op()("Z")

    result = backend.estimator(options={"default_shots": 32}).run([(circuit, observable)]).result()[0]  # ty: ignore
    estimator_data = cast("_EstimatorDataLike", result.data)
    expectation_value = float(estimator_data.evs[()])
    standard_deviation = float(estimator_data.stds[()])

    assert -1.0 <= expectation_value <= 1.0
    assert standard_deviation >= 0.0
    metadata = cast("dict[str, int]", result.metadata)
    assert metadata["shots"] == 32
