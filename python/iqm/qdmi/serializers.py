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
import warnings
from typing import TYPE_CHECKING, Any

try:
    from mqt.core.plugins.qiskit import serializers as core_serializers
    from mqt.core.plugins.qiskit.exceptions import TranslationError, UnsupportedOperationError
    from qiskit.circuit.library import Barrier, CZGate, Measure, RGate
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from .gates import MoveGate
from .qiskit import IQMBackend

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from qiskit.circuit import QuantumCircuit

__all__ = ["qiskit_to_iqm_json"]


def __dir__() -> list[str]:
    return __all__


def _validate_metadata_keys(value: object) -> None:
    """Reject object keys that JSON encoding would silently convert to strings.

    Args:
        value: A metadata value to check recursively.

    Raises:
        TypeError: If an object key is not a string.
    """
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                msg = "Metadata object keys must be strings."
                raise TypeError(msg)
            _validate_metadata_keys(child)
    elif isinstance(value, list | tuple):
        for child in value:
            _validate_metadata_keys(child)


def _json_metadata(metadata: dict[str, Any]) -> dict[str, Any]:
    """Return metadata that encodes safely as JSON, or drop it with a warning.

    Args:
        metadata: Circuit metadata to check without mutating it.

    Returns:
        The metadata itself, or an empty dictionary if it cannot be encoded.
    """
    try:
        # Encode first to reject circular references before traversing the keys.
        json.dumps(metadata, allow_nan=False)
        _validate_metadata_keys(metadata)
    except (TypeError, ValueError, RecursionError) as exc:
        warnings.warn(f"Dropping circuit metadata that cannot be encoded as JSON: {exc}", stacklevel=3)
        return {}
    return metadata


def qiskit_to_iqm_json(circuit: QuantumCircuit, backend: QDMIBackend) -> str | core_serializers.SerializedProgram:  # ty: ignore[unresolved-attribute]
    """Serialize a Qiskit :class:`~qiskit.circuit.QuantumCircuit` into IQM JSON.

    The IQM JSON format is a device-specific format that encodes quantum operations
    as JSON objects with site names, operation names, and arguments. Circuit
    metadata is preserved using Python's JSON encoding (including tuples as
    arrays), with string object keys and finite numbers required at every level.
    Metadata that cannot be encoded is dropped with a warning; the circuit and
    its metadata are not modified.

    Measurements must be terminal and each qubit may be measured once. With
    Core's output-mapping API, source classical bits retain initialized zeros
    and the final write to each destination. Older Core versions require complete
    readout and emit measurements in classical-bit order. Barriers in the readout
    suffix precede the measurements on that compatibility path.

    Note:
        The serialization currently supports only operations that are natively
        supported by the IQM hardware. Unsupported operations will raise
        :class:`~mqt.core.plugins.qiskit.exceptions.UnsupportedOperationError`.

    Args:
        circuit: The Qiskit quantum circuit to serialize.
        backend: The backend that runs the circuit. Its device provides the site
            names the format uses as loci. For :class:`~iqm.qdmi.qiskit.IQMBackend`,
            circuit indices follow the calibrated target's site order.

    Returns:
        IQM JSON and source-classical mapping when Core supports it, otherwise
        a JSON string with measurements ordered by classical destination.

    Raises:
        UnsupportedOperationError: If the circuit contains operations not supported
            by IQM hardware.
        TranslationError: If the serialization fails.

    Examples:
        >>> from qiskit import QuantumCircuit
        >>> import numpy as np
        >>> from iqm.qdmi.serializers import qiskit_to_iqm_json
        >>> qc = QuantumCircuit(2)
        >>> qc.r(np.pi / 2, 0, 0)
        >>> qc.cz(0, 1)
        >>> qc.measure_all()
        >>> json_str = qiskit_to_iqm_json(qc, backend)
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
        if isinstance(backend, IQMBackend):
            sites = [sites[index] for index in backend.physical_qubits]
        instructions: list[dict[str, Any]] = []
        measurements: dict[int, dict[str, Any]] = {}
        measured_qubits: set[int] = set()
        serialized_program = getattr(core_serializers, "SerializedProgram", None)
        clbit_indices: list[int | None] = [None] * circuit.num_clbits
        output_width = 0

        for instruction in circuit.data:
            operation, qargs, cargs = instruction.operation, instruction.qubits, instruction.clbits

            if measured_qubits and not isinstance(operation, Measure | Barrier):
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

                if serialized_program is not None:
                    key = f"m{output_width}"
                else:
                    if not bitloc.registers:
                        _raise_error(
                            TranslationError,
                            "An unregistered classical bit requires MQT Core with SerializedProgram support.",
                        )
                    creg = bitloc.registers[0][0]
                    creg_idx = circuit.cregs.index(creg)
                    clbit_index = bitloc.registers[0][1]
                    key = f"{creg.name}_{len(creg)}_{creg_idx}_{clbit_index}"
                qubit_index = circuit.find_bit(qargs[0]).index
                if qubit_index in measured_qubits or (serialized_program is None and bitloc.index in measurements):
                    _raise_error(TranslationError, "Each qubit and classical bit may be measured only once.")
                measured_qubits.add(qubit_index)
                measurement = {
                    "name": "measure",
                    "locus": [sites[qubit_index].name()],
                    "args": {
                        "key": key,
                    },
                }
                if serialized_program is not None:
                    instructions.append(measurement)
                    clbit_indices[bitloc.index] = output_width
                    output_width += 1
                else:
                    measurements[bitloc.index] = measurement

            # Unsupported operation
            else:
                msg = f"Operation '{operation.name}' is not supported in IQM JSON format"
                _raise_error(UnsupportedOperationError, msg)

        if serialized_program is None and len(measurements) != circuit.num_clbits:
            _raise_error(TranslationError, "Every classical bit must be measured exactly once.")
        if serialized_program is None:
            instructions.extend(measurements[index] for index in range(circuit.num_clbits))

        program: dict[str, Any] = {
            "name": circuit.name or "circuit",
            "metadata": _json_metadata(circuit.metadata),
            "instructions": instructions,
        }

        payload = json.dumps(program)
        if serialized_program is not None:
            return serialized_program(payload, output_width, tuple(clbit_indices))
        return payload  # ruff: ignore[try-consider-else]

    except UnsupportedOperationError:
        raise

    except Exception as exc:
        msg = f"Failed to serialize the circuit to IQM JSON: {exc}"
        raise TranslationError(msg) from exc
