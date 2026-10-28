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

"""Serialization of Qiskit circuits into the IQM JSON program format.

MQT Core loads :func:`qiskit_to_iqm_json` through the
``mqt.core.qiskit.program_serializers`` entry point, so any QDMI backend over an
IQM device submits IQM JSON without naming this package.
"""

from __future__ import annotations

import json
from typing import TYPE_CHECKING, Any

try:
    from mqt.core.mlir import TerminalReadout, serialize_program
    from mqt.core.plugins.qiskit.exceptions import TranslationError, UnsupportedOperationError
    from mqt.core.qdmi import ProgramFormat
    from qiskit.circuit.library import Barrier, CZGate, Measure, RGate
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from .gates import MoveGate

if TYPE_CHECKING:
    from mqt.core.mlir import CompiledProgram
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from qiskit.circuit import QuantumCircuit

__all__ = ["qiskit_to_iqm_json"]


def __dir__() -> list[str]:
    return __all__


def qiskit_to_iqm_json(circuit: QuantumCircuit, backend: QDMIBackend) -> CompiledProgram | str:
    """Serialize native instructions through MQT Core without compiling them.

    The shared compiler preserves terminal readout destinations, including
    repeated measurements and untouched classical bits, in its result layout.
    Circuits containing MOVE use the existing resonator serializer, which
    requires one measurement per qubit and classical bit. Circuit metadata stays
    in the local Qiskit result header; it is not sent to the service.

    Args:
        circuit: A bound circuit using the target's native gates and indices.
        backend: The backend supplying device sites and calibrated target indices.

    Returns:
        A serialized program with its result layout, or IQM JSON for MOVE.

    Raises:
        mqt.core.plugins.qiskit.exceptions.TranslationError: Native serialization or the readout layout is unsupported.
    """
    if any(isinstance(instruction.operation, MoveGate) for instruction in circuit.data):
        return _serialize_move_circuit(circuit, backend)
    try:
        return serialize_program(
            circuit,
            target=backend.device,
            program_format=ProgramFormat.IQM_JSON,
            terminal_readout=TerminalReadout.MEASURED,
        )
    except (RuntimeError, ValueError) as exc:
        msg = f"Failed to serialize the circuit to IQM JSON: {exc}"
        raise TranslationError(msg) from exc


def _serialize_move_circuit(circuit: QuantumCircuit, backend: QDMIBackend) -> str:
    """Retain native MOVE and resonator support until Core can represent it.

    Returns:
        IQM JSON with one terminal measurement per classical bit.

    Raises:
        UnsupportedOperationError: A gate or parameter is unsupported.
        mqt.core.plugins.qiskit.exceptions.TranslationError: The measurement layout or a site is unsupported.
    """

    def _raise_error(exception_type: type[Exception], message: str) -> None:
        """Helper to raise exceptions (satisfies TRY301).

        Args:
            exception_type: The type of exception to raise.
            message: The error message.
        """
        raise exception_type(message)

    try:  # ruff:ignore[too-many-statements-in-try-clause]
        # Check for unbound parameters
        if circuit.parameters:
            param_names = ", ".join(sorted(p.name for p in circuit.parameters))
            msg = (
                f"Circuit contains unbound parameters: {param_names}. "
                "All parameters must be bound to numeric values before serialization to IQM JSON. "
                "Use circuit.assign_parameters() to bind parameters."
            )
            _raise_error(UnsupportedOperationError, msg)

        sites = backend.device.sites()
        instructions: list[dict[str, Any]] = []
        measurements: dict[int, dict[str, Any]] = {}
        measured_qubits: set[int] = set()

        for instruction in circuit.data:
            operation, qargs, cargs = instruction.operation, instruction.qubits, instruction.clbits

            if measurements and not isinstance(operation, Measure | Barrier):
                _raise_error(UnsupportedOperationError, "IQM JSON requires terminal measurements.")

            # R gate (PRX in IQM terminology)
            if isinstance(operation, RGate):
                qubit_index = circuit.find_bit(qargs[0]).index
                instructions.append({
                    "name": "prx",
                    "locus": [sites[qubit_index].name()],
                    "args": {
                        "angle": float(operation.params[0]),
                        "phase": float(operation.params[1]),
                    },
                })

            # CZ or Move gate
            elif isinstance(operation, CZGate | MoveGate):
                qubit_index1: int = circuit.find_bit(qargs[0]).index
                qubit_index2: int = circuit.find_bit(qargs[1]).index
                instructions.append({
                    "name": operation.name,
                    "locus": [
                        sites[qubit_index1].name(),
                        sites[qubit_index2].name(),
                    ],
                    "args": {},
                })

            # Barrier
            elif isinstance(operation, Barrier):
                qubit_indices: list[int] = []
                for qubit in qargs:
                    qubit_index = circuit.find_bit(qubit).index
                    qubit_indices.append(qubit_index)
                instructions.append({
                    "name": "barrier",
                    "locus": [sites[i].name() for i in qubit_indices],
                    "args": {},
                })

            # Measure
            elif isinstance(operation, Measure):
                clbit = cargs[0]
                bitloc = circuit.find_bit(clbit)

                # Check if classical bit is part of a register
                if not bitloc.registers:
                    msg = (
                        "Measurement of unregistered classical bit is unsupported by IQM JSON export. "
                        "All classical bits must be part of a ClassicalRegister."
                    )
                    _raise_error(TranslationError, msg)

                creg = bitloc.registers[0][0]
                creg_idx = circuit.cregs.index(creg)
                clbit_index = bitloc.registers[0][1]
                key = f"{creg.name}_{len(creg)}_{creg_idx}_{clbit_index}"
                qubit_index = circuit.find_bit(qargs[0]).index
                if bitloc.index in measurements or qubit_index in measured_qubits:
                    _raise_error(TranslationError, "Each qubit and classical bit may be measured only once.")
                measured_qubits.add(qubit_index)
                measurements[bitloc.index] = {
                    "name": "measure",
                    "locus": [sites[qubit_index].name()],
                    "args": {
                        "key": key,
                    },
                }

            # Unsupported operation
            else:
                msg = f"Operation '{operation.name}' is not supported in IQM JSON format"
                _raise_error(UnsupportedOperationError, msg)

        if len(measurements) != circuit.num_clbits:
            _raise_error(TranslationError, "Every classical bit must be measured exactly once.")
        instructions.extend(measurements[index] for index in range(circuit.num_clbits))

        program: dict[str, Any] = {
            "name": circuit.name or "circuit",
            "metadata": {},
            "instructions": instructions,
        }

        return json.dumps(program)

    except UnsupportedOperationError:
        raise

    except Exception as exc:
        msg = f"Failed to serialize the circuit to IQM JSON: {exc}"
        raise TranslationError(msg) from exc
