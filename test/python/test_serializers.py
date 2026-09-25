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
from typing import TYPE_CHECKING, Any
from unittest.mock import Mock

import numpy as np
import pytest
from mqt.core.plugins.qiskit import serializers as core_serializers
from mqt.core.plugins.qiskit.exceptions import TranslationError, UnsupportedOperationError
from mqt.core.plugins.qiskit.job import QDMIJob
from mqt.core.qdmi import Job
from qiskit import transpile
from qiskit.circuit import ClassicalRegister, Clbit, Parameter, QuantumCircuit, QuantumRegister
from qiskit.providers.basic_provider import BasicSimulator

from iqm.qdmi.gates import MoveGate
from iqm.qdmi.serializers import qiskit_to_iqm_json

if TYPE_CHECKING:
    from collections.abc import Callable


def _program(serialized: str | core_serializers.SerializedProgram) -> dict[str, Any]:  # ty: ignore[unresolved-attribute]
    """Read the circuit from either supported Core serializer representation.

    Returns:
        The decoded circuit payload.
    """
    return json.loads(serialized if isinstance(serialized, str) else serialized.payload)


class StubSite:
    """A device site that reports its name."""

    def __init__(self, index: int) -> None:
        """Initialize the site.

        Args:
            index: Position of the site on the device.
        """
        self._index = index

    def index(self) -> int:
        """Return the program index."""
        return self._index

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


def test_simple_circuit(backend: Callable[[int], StubBackend]) -> None:
    """A circuit of native operations serializes to an IQM JSON program."""
    qc = QuantumCircuit(2, 2)
    qc.r(np.pi / 2, 0.0, 0)
    qc.cz(0, 1)
    qc.measure([0, 1], [0, 1])

    program = _program(qiskit_to_iqm_json(qc, backend(2)))  # ty: ignore[invalid-argument-type]

    assert [instr["name"] for instr in program["instructions"]] == ["prx", "cz", "measure", "measure"]
    assert program["metadata"] == {}


@pytest.mark.parametrize(
    ("angle", "phase"),
    [(0.0, 0.0), (np.pi, np.pi / 2), (-np.pi / 3, -np.pi / 4), (5 * np.pi, -7 * np.pi), (0.123, 0.456)],
)
def test_prx_parameters(backend: Callable[[int], StubBackend], angle: float, phase: float) -> None:
    """An R gate preserves radians in the current IQM circuit format."""
    qc = QuantumCircuit(1)
    qc.r(angle, phase, 0)

    program = _program(qiskit_to_iqm_json(qc, backend(1)))  # ty: ignore[invalid-argument-type]

    prx = program["instructions"][0]
    assert prx["name"] == "prx"
    assert prx["locus"] == ["QB1"]
    assert prx["args"] == pytest.approx({"angle": angle, "phase": phase})


def test_barrier(backend: Callable[[int], StubBackend]) -> None:
    """A barrier keeps every site it spans in its locus."""
    qc = QuantumCircuit(3)
    qc.barrier([0, 2])

    program = _program(qiskit_to_iqm_json(qc, backend(3)))  # ty: ignore[invalid-argument-type]

    barrier = program["instructions"][0]
    assert barrier["name"] == "barrier"
    assert barrier["locus"] == ["QB1", "QB3"]
    assert barrier["args"] == {}


def test_cz_gate(backend: Callable[[int], StubBackend]) -> None:
    """A CZ gate becomes a two-site cz instruction."""
    qc = QuantumCircuit(2)
    qc.cz(0, 1)

    program = _program(qiskit_to_iqm_json(qc, backend(2)))  # ty: ignore[invalid-argument-type]

    cz = program["instructions"][0]
    assert cz["name"] == "cz"
    assert cz["locus"] == ["QB1", "QB2"]
    assert cz["args"] == {}


def test_move_gate(backend: Callable[[int], StubBackend]) -> None:
    """A MOVE gate becomes a two-site move instruction."""
    qc = QuantumCircuit(2)
    qc.append(MoveGate(), [0, 1])

    program = _program(qiskit_to_iqm_json(qc, backend(2)))  # ty: ignore[invalid-argument-type]

    move = program["instructions"][0]
    assert move["name"] == "move"
    assert move["locus"] == ["QB1", "QB2"]
    assert move["args"] == {}


def test_measure_keys_are_unique(backend: Callable[[int], StubBackend]) -> None:
    """Each measurement carries a unique instruction key."""
    qc = QuantumCircuit(2, 2)
    qc.cz(0, 1)
    qc.measure([0, 1], [0, 1])

    program = _program(qiskit_to_iqm_json(qc, backend(2)))  # ty: ignore[invalid-argument-type]

    keys = [instr["args"]["key"] for instr in program["instructions"] if instr["name"] == "measure"]
    assert len(keys) == 2
    assert len(set(keys)) == 2


def test_circuit_name_is_preserved(backend: Callable[[int], StubBackend]) -> None:
    """The program carries the circuit name."""
    qc = QuantumCircuit(1, name="my_circuit")
    qc.r(0.0, 0.0, 0)

    program = _program(qiskit_to_iqm_json(qc, backend(1)))  # ty: ignore[invalid-argument-type]

    assert program["name"] == "my_circuit"


def test_bound_parameters(backend: Callable[[int], StubBackend]) -> None:
    """A circuit serializes once its parameters are bound."""
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.r(theta, 0.0, 0)

    program = _program(qiskit_to_iqm_json(qc.assign_parameters({theta: np.pi}), backend(1)))  # ty: ignore[invalid-argument-type]

    assert program["instructions"][0]["args"]["angle"] == pytest.approx(np.pi)


def test_unbound_parameters_are_rejected(backend: Callable[[int], StubBackend]) -> None:
    """An unbound parameter fails the conversion with a message naming it."""
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.r(theta, 0.0, 0)

    with pytest.raises(UnsupportedOperationError, match="unbound parameters: theta"):
        qiskit_to_iqm_json(qc, backend(1))  # ty: ignore[invalid-argument-type]


def test_unsupported_operation_is_rejected(backend: Callable[[int], StubBackend]) -> None:
    """An operation outside the IQM native gate set fails the conversion."""
    qc = QuantumCircuit(1)
    qc.h(0)

    with pytest.raises(UnsupportedOperationError, match="not supported in IQM JSON format"):
        qiskit_to_iqm_json(qc, backend(1))  # ty: ignore[invalid-argument-type]


def test_unregistered_classical_bit(backend: Callable[[int], StubBackend]) -> None:
    """Loose classical bits have the same identity mapping as registered bits."""
    qc = QuantumCircuit(QuantumRegister(1), [Clbit()])
    qc.measure(0, 0)
    program = _program(qiskit_to_iqm_json(qc, backend(1)))  # ty: ignore[invalid-argument-type]
    assert program["instructions"][0]["args"]["key"] == "m0"


def test_registered_classical_bit(backend: Callable[[int], StubBackend]) -> None:
    """Measurement keys identify instructions independently of register names."""
    creg = ClassicalRegister(1, "result")
    qc = QuantumCircuit(QuantumRegister(1), creg)
    qc.measure(0, 0)

    program = _program(qiskit_to_iqm_json(qc, backend(1)))  # ty: ignore[invalid-argument-type]

    assert program["instructions"][0]["args"]["key"] == "m0"


def test_serialization_failure_becomes_translation_error(backend: Callable[[int], StubBackend]) -> None:
    """A failure inside the conversion surfaces as a TranslationError."""
    qc = QuantumCircuit(2)
    qc.cz(0, 1)

    with pytest.raises(TranslationError, match="Failed to serialize the circuit to IQM JSON"):
        qiskit_to_iqm_json(qc, backend(1))  # ty: ignore[invalid-argument-type]


def test_serializer_is_advertised_to_mqt_core() -> None:
    """MQT Core discovers the IQM JSON serializer through the entry point group."""
    from mqt.core.plugins.qiskit import program_serializer  # ruff:ignore[import-outside-top-level]
    from mqt.core.qdmi import ProgramFormat  # ruff:ignore[import-outside-top-level]

    assert program_serializer(ProgramFormat.IQM_JSON) is qiskit_to_iqm_json


def test_source_classical_layout(backend: Callable[[int], StubBackend]) -> None:
    """Keep register widths, initialized holes, and the last write to each bit."""
    qc = QuantumCircuit(QuantumRegister(2), ClassicalRegister(3, "a"), ClassicalRegister(2, "b"))
    qc.measure(0, 2)
    qc.measure(1, 4)
    qc.measure(1, 2)
    if not hasattr(core_serializers, "SerializedProgram"):
        with pytest.raises(TranslationError, match="Upgrade MQT Core"):
            qiskit_to_iqm_json(qc, backend(2))  # ty: ignore[invalid-argument-type]
        return
    serialized = qiskit_to_iqm_json(qc, backend(2))  # ty: ignore[invalid-argument-type]
    assert not isinstance(serialized, str)
    assert serialized.output_width == 3
    assert serialized.clbit_indices == (None, None, 2, None, 1)
    keys = [i["args"]["key"] for i in _program(serialized)["instructions"]]
    assert keys == ["m0", "m1", "m2"]


@pytest.mark.skipif(not hasattr(core_serializers, "SerializedProgram"), reason="Requires Core output-mapping API")
@pytest.mark.parametrize("memory", [False, True])
def test_qiskit_result_matches_basic_simulator(backend: Callable[[int], StubBackend], *, memory: bool) -> None:
    """Reconstruct IQM raw outputs into the same source result as native Qiskit."""
    circuit = QuantumCircuit(QuantumRegister(3), ClassicalRegister(3, "a"), ClassicalRegister(2, "b"))
    circuit.r(np.pi, 0, 0)
    circuit.r(np.pi, 0, 1)
    circuit.measure(0, 2)
    circuit.measure(1, 4)
    circuit.measure(2, 2)
    serialized = qiskit_to_iqm_json(circuit, backend(3))  # ty: ignore[invalid-argument-type]
    handle = Mock(spec=Job)
    handle.id = "local-result"
    handle.check.return_value = Job.Status.DONE
    handle.get_shots.return_value = ["011"] * 8
    handle.get_counts.return_value = {"011": 8}
    provider = Mock(name="iqm")
    provider.name = "iqm"
    provider.backend_version = "test"
    actual = QDMIJob(
        provider,
        [handle],
        [circuit],
        shots=8,
        memory=memory,
        output_mappings=[serialized],  # ty: ignore[unknown-argument]
    ).result()
    simulator = BasicSimulator()
    expected = simulator.run(transpile(circuit, simulator), shots=8, memory=memory).result()
    assert actual.get_counts() == expected.get_counts() == {"10 000": 8}
    if memory:
        assert actual.get_memory() == expected.get_memory()
