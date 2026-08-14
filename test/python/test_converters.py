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

"""Tests for the conversion of Qiskit circuits into IQM JSON."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

import numpy as np
import pytest
from mqt.core.plugins.qiskit.exceptions import TranslationError, UnsupportedOperationError
from qiskit.circuit import ClassicalRegister, Clbit, Parameter, QuantumCircuit, QuantumRegister

from iqm.qdmi.converters import qiskit_to_iqm_json
from iqm.qdmi.gates import MoveGate

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
    """The part of a QDMI device the IQM JSON conversion uses."""

    def __init__(self, num_sites: int) -> None:
        """Initialize the device.

        Args:
            num_sites: Number of sites the device exposes.
        """
        self._sites = [StubSite(index) for index in range(num_sites)]

    def sites(self) -> list[StubSite]:
        """Returns the device sites in index order."""
        return self._sites


@pytest.fixture
def device() -> Callable[[int], StubDevice]:
    """Returns a factory for stub devices with a given number of sites."""
    return StubDevice


def test_simple_circuit(device: Callable[[int], StubDevice]) -> None:
    """A circuit of native operations converts to an IQM JSON program."""
    qc = QuantumCircuit(2, 2)
    qc.r(np.pi / 2, 0.0, 0)
    qc.cz(0, 1)
    qc.measure([0, 1], [0, 1])

    program = json.loads(qiskit_to_iqm_json(qc, device(2)))  # ty: ignore[invalid-argument-type]

    assert [instr["name"] for instr in program["instructions"]] == ["prx", "cz", "measure", "measure"]
    assert program["metadata"] == {}


def test_prx_parameters(device: Callable[[int], StubDevice]) -> None:
    """An R gate becomes a prx instruction with angles in turns."""
    qc = QuantumCircuit(1)
    qc.r(np.pi, np.pi / 2, 0)

    program = json.loads(qiskit_to_iqm_json(qc, device(1)))  # ty: ignore[invalid-argument-type]

    prx = program["instructions"][0]
    assert prx["name"] == "prx"
    assert prx["locus"] == ["QB1"]
    assert prx["args"]["angle_t"] == pytest.approx(0.5)
    assert prx["args"]["phase_t"] == pytest.approx(0.25)


def test_barrier(device: Callable[[int], StubDevice]) -> None:
    """A barrier keeps every site it spans in its locus."""
    qc = QuantumCircuit(3)
    qc.barrier([0, 2])

    program = json.loads(qiskit_to_iqm_json(qc, device(3)))  # ty: ignore[invalid-argument-type]

    barrier = program["instructions"][0]
    assert barrier["name"] == "barrier"
    assert barrier["locus"] == ["QB1", "QB3"]
    assert barrier["args"] == {}


def test_cz_gate(device: Callable[[int], StubDevice]) -> None:
    """A CZ gate becomes a two-site cz instruction."""
    qc = QuantumCircuit(2)
    qc.cz(0, 1)

    program = json.loads(qiskit_to_iqm_json(qc, device(2)))  # ty: ignore[invalid-argument-type]

    cz = program["instructions"][0]
    assert cz["name"] == "cz"
    assert cz["locus"] == ["QB1", "QB2"]
    assert cz["args"] == {}


def test_move_gate(device: Callable[[int], StubDevice]) -> None:
    """A MOVE gate becomes a two-site move instruction."""
    qc = QuantumCircuit(2)
    qc.append(MoveGate(), [0, 1])

    program = json.loads(qiskit_to_iqm_json(qc, device(2)))  # ty: ignore[invalid-argument-type]

    move = program["instructions"][0]
    assert move["name"] == "move"
    assert move["locus"] == ["QB1", "QB2"]
    assert move["args"] == {}


def test_measure_keys_are_unique(device: Callable[[int], StubDevice]) -> None:
    """Each measurement carries a key derived from its classical register position."""
    qc = QuantumCircuit(2, 2)
    qc.cz(0, 1)
    qc.measure([0, 1], [0, 1])

    program = json.loads(qiskit_to_iqm_json(qc, device(2)))  # ty: ignore[invalid-argument-type]

    keys = [instr["args"]["key"] for instr in program["instructions"] if instr["name"] == "measure"]
    assert len(keys) == 2
    assert len(set(keys)) == 2


def test_circuit_name_is_preserved(device: Callable[[int], StubDevice]) -> None:
    """The program carries the circuit name."""
    qc = QuantumCircuit(1, name="my_circuit")
    qc.r(0.0, 0.0, 0)

    program = json.loads(qiskit_to_iqm_json(qc, device(1)))  # ty: ignore[invalid-argument-type]

    assert program["name"] == "my_circuit"


def test_bound_parameters(device: Callable[[int], StubDevice]) -> None:
    """A circuit converts once its parameters are bound."""
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.r(theta, 0.0, 0)

    program = json.loads(qiskit_to_iqm_json(qc.assign_parameters({theta: np.pi}), device(1)))  # ty: ignore[invalid-argument-type]

    assert program["instructions"][0]["args"]["angle_t"] == pytest.approx(0.5)


def test_unbound_parameters_are_rejected(device: Callable[[int], StubDevice]) -> None:
    """An unbound parameter fails the conversion with a message naming it."""
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.r(theta, 0.0, 0)

    with pytest.raises(UnsupportedOperationError, match="unbound parameters: theta"):
        qiskit_to_iqm_json(qc, device(1))  # ty: ignore[invalid-argument-type]


def test_unsupported_operation_is_rejected(device: Callable[[int], StubDevice]) -> None:
    """An operation outside the IQM native gate set fails the conversion."""
    qc = QuantumCircuit(1)
    qc.h(0)

    with pytest.raises(UnsupportedOperationError, match="not supported in IQM JSON format"):
        qiskit_to_iqm_json(qc, device(1))  # ty: ignore[invalid-argument-type]


def test_unregistered_classical_bit_is_rejected(device: Callable[[int], StubDevice]) -> None:
    """Measuring into a loose classical bit fails the conversion."""
    qc = QuantumCircuit(QuantumRegister(1), [Clbit()])
    qc.measure(0, 0)

    with pytest.raises(TranslationError, match="unregistered classical bit"):
        qiskit_to_iqm_json(qc, device(1))  # ty: ignore[invalid-argument-type]


def test_registered_classical_bit(device: Callable[[int], StubDevice]) -> None:
    """Measuring into a named register encodes the register in the key."""
    creg = ClassicalRegister(1, "result")
    qc = QuantumCircuit(QuantumRegister(1), creg)
    qc.measure(0, 0)

    program = json.loads(qiskit_to_iqm_json(qc, device(1)))  # ty: ignore[invalid-argument-type]

    assert program["instructions"][0]["args"]["key"] == "result_1_0_0"


def test_conversion_failure_becomes_translation_error(device: Callable[[int], StubDevice]) -> None:
    """A failure inside the conversion surfaces as a TranslationError."""
    qc = QuantumCircuit(2)
    qc.cz(0, 1)

    with pytest.raises(TranslationError, match="Failed to convert circuit to IQM JSON"):
        qiskit_to_iqm_json(qc, device(1))  # ty: ignore[invalid-argument-type]


def test_codec_is_advertised_to_mqt_core() -> None:
    """MQT Core discovers the IQM JSON codec through the entry point group."""
    from mqt.core.plugins.qiskit import program_codec  # ruff:ignore[import-outside-top-level]
    from mqt.core.qdmi import ProgramFormat  # ruff:ignore[import-outside-top-level]

    assert program_codec(ProgramFormat.IQM_JSON) is qiskit_to_iqm_json
