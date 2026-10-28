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

"""Tests for the serialization of Qiskit circuits into IQM JSON."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

import numpy as np
import pytest
from mqt.core.mlir import CompiledProgram
from mqt.core.plugins.qiskit.exceptions import TranslationError
from qiskit.circuit import ClassicalRegister, Parameter, QuantumCircuit, QuantumRegister

from iqm.qdmi.gates import MoveGate
from iqm.qdmi.serializers import qiskit_to_iqm_json

pytestmark = pytest.mark.usefixtures("serialization_target")


if TYPE_CHECKING:
    from collections.abc import Callable


class StubSite:
    """A device site that reports its name."""

    def __init__(self, index: int) -> None:
        """Initialize the site.

        Args:
            index: Position of the site on the device.
        """
        self._index = index

    def name(self) -> str:
        """Returns the site name the IQM JSON format uses as a locus entry."""
        return f"QB{self._index + 1}"


class StubDevice:
    """The part of a QDMI device the IQM JSON serialization uses."""

    def __init__(self, num_sites: int) -> None:
        """Initialize the device.

        Args:
            num_sites: Number of sites the device exposes.
        """
        self._sites = [StubSite(index) for index in range(num_sites)]

    def sites(self) -> list[StubSite]:
        """Returns the device sites in index order."""
        return self._sites


class StubBackend:
    """The part of a QDMI backend the IQM JSON serialization uses.

    A program serializer receives the backend and reaches the device through
    its :attr:`~mqt.core.plugins.qiskit.backend.QDMIBackend.device` property.
    """

    def __init__(self, num_sites: int) -> None:
        """Initialize the backend.

        Args:
            num_sites: Number of sites the backend's device exposes.
        """
        self._device = StubDevice(num_sites)

    @property
    def device(self) -> StubDevice:
        """The device the backend runs on."""
        return self._device


@pytest.fixture
def backend() -> Callable[[int], StubBackend]:
    """Returns a factory for stub backends with a given number of device sites."""
    return StubBackend


def test_native_circuit_and_readout_order(backend: Callable[[int], StubBackend]) -> None:
    """Native gates and complete multi-register readout use physical site names."""
    qc = QuantumCircuit(QuantumRegister(3), ClassicalRegister(2, "left"), ClassicalRegister(1, "right"), name="native")
    qc.r(-np.pi / 3, np.pi / 4, 0)
    qc.cz(0, 1)
    qc.append(MoveGate(), [1, 2])
    qc.measure(0, 2)
    qc.barrier([0, 2])
    qc.measure([1, 2], [0, 1])
    original = qc.copy()

    program = json.loads(qiskit_to_iqm_json(qc, backend(3)))  # ty: ignore[invalid-argument-type]

    assert program == {
        "name": "native",
        "metadata": {},
        "instructions": [
            {"name": "prx", "locus": ["QB1"], "args": {"angle": -np.pi / 3, "phase": np.pi / 4}},
            {"name": "cz", "locus": ["QB1", "QB2"], "args": {}},
            {"name": "move", "locus": ["QB2", "QB3"], "args": {}},
            {"name": "barrier", "locus": ["QB1", "QB3"], "args": {}},
            {"name": "measure", "locus": ["QB2"], "args": {"key": "left_2_0_0"}},
            {"name": "measure", "locus": ["QB3"], "args": {"key": "left_2_0_1"}},
            {"name": "measure", "locus": ["QB1"], "args": {"key": "right_1_1_0"}},
        ],
    }
    assert qc == original


def test_parameter_binding(backend: Callable[[int], StubBackend]) -> None:
    """Parameters must be bound before serialization."""
    theta = Parameter("theta")
    qc = QuantumCircuit(1, 1)
    qc.r(theta, 0.0, 0)
    qc.measure(0, 0)
    with pytest.raises(TranslationError, match="parameter"):
        qiskit_to_iqm_json(qc, backend(1))  # ty: ignore[invalid-argument-type]

    compiled = qiskit_to_iqm_json(qc.assign_parameters({theta: np.pi}), backend(1))  # ty: ignore[invalid-argument-type]
    assert isinstance(compiled, CompiledProgram)
    program = json.loads(compiled.payload)
    assert program["instructions"][0]["args"]["angle"] == pytest.approx(np.pi)


@pytest.mark.parametrize("case", ["unused", "overwrite", "repeated"])
def test_terminal_readout_layout(backend: Callable[[int], StubBackend], case: str) -> None:
    """The native compiler retains zeros, final writes, and shared measurements."""
    qc = QuantumCircuit(2, 3)
    qc.r(np.pi, 0, 0)
    qc.measure(0, 2)
    if case == "overwrite":
        qc.measure(1, 2)
    elif case == "repeated":
        qc.measure(0, 0)
    compiled = qiskit_to_iqm_json(qc, backend(2))  # ty: ignore[invalid-argument-type]
    assert isinstance(compiled, CompiledProgram)
    assert compiled.result_layout is not None
    layout = compiled.result_layout
    assert len(layout.bits) == 3
    assert layout.bits[1] is None
    if case == "repeated":
        assert layout.bits[0] == layout.bits[2]
    else:
        assert layout.bits[0] is None
    program = json.loads(compiled.payload)
    measured = [instruction for instruction in program["instructions"] if instruction["name"] == "measure"]
    assert len(measured) == layout.raw_width
    assert len({instruction["locus"][0] for instruction in measured}) == layout.raw_width
    raw = [instruction["locus"] == ["QB1"] for instruction in measured]
    decoded = "".join(str(int(False if bit is None else raw[bit])) for bit in reversed(layout.bits))
    assert decoded == {"unused": "100", "overwrite": "000", "repeated": "101"}[case]


@pytest.mark.parametrize("case", ["intermediate", "gate", "site"])
def test_unsupported_circuits(backend: Callable[[int], StubBackend], case: str) -> None:
    """Unsupported instructions and sites fail before submission."""
    qc = QuantumCircuit(2, 2)
    sites = 2
    if case == "intermediate":
        qc.measure(0, 0)
        qc.r(0.5, 0, 0)
    elif case == "gate":
        qc.h(0)
    else:
        qc.cz(0, 1)
        sites = 1
    qc.measure(1, 1)
    with pytest.raises(TranslationError):
        qiskit_to_iqm_json(qc, backend(sites))  # ty: ignore[invalid-argument-type]


def test_serializer_is_advertised_to_mqt_core() -> None:
    """MQT Core discovers the IQM JSON serializer through the entry point group."""
    from mqt.core.plugins.qiskit import program_serializer  # ruff:ignore[import-outside-top-level]
    from mqt.core.qdmi import ProgramFormat  # ruff:ignore[import-outside-top-level]

    assert program_serializer(ProgramFormat.IQM_JSON) is qiskit_to_iqm_json


def test_metadata_remains_local(backend: Callable[[int], StubBackend]) -> None:
    """Arbitrary source metadata is untouched and omitted from the cloud payload."""
    metadata = {"experiment": object()}
    qc = QuantumCircuit(1, 1, metadata=metadata)
    qc.measure(0, 0)
    compiled = qiskit_to_iqm_json(qc, backend(1))  # ty: ignore[invalid-argument-type]
    assert isinstance(compiled, CompiledProgram)
    assert not json.loads(compiled.payload).get("metadata")
    assert qc.metadata == metadata
