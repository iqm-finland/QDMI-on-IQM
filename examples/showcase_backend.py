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

"""Share backend selection and reservation cleanup between the hybrid showcases."""

from __future__ import annotations

from contextlib import contextmanager
from typing import TYPE_CHECKING

from mqt.core.plugins.qiskit.backend import QDMIBackend
from qiskit import ClassicalRegister

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    import argparse
    from collections.abc import Generator

    from qiskit import QuantumCircuit


def add_backend_arguments(parser: argparse.ArgumentParser) -> None:
    """Add the common showcase backend and iteration flags."""
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--iteration-mode", action="store_true", help="Reserve the IQM quantum computer for this run.")


def validate_backend_arguments(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """Reject simulator reservations before preparing the workload."""
    if args.iteration_mode and args.backend == "sim":
        parser.error("--iteration-mode requires --backend iqm")


@contextmanager
def showcase_backend(backend: str, *, num_qubits: int, iteration_mode: bool) -> Generator[QDMIBackend, None, None]:
    """Open the showcase backend for the duration of the quantum workload.

    Yields:
        The backend, attached to a reservation when requested.

    """
    if backend == "sim":
        yield QDMIBackend.from_device_id("mqt.ddsim.default", custom1=str(num_qubits))
        return
    iqm = IQMBackend()
    if iteration_mode:
        with iqm.reservation(max_duration_seconds=900, idle_timeout_seconds=120) as reserved:
            yield reserved
    else:
        yield iqm


def measure_logical_qubits(circuit: QuantumCircuit, num_qubits: int) -> None:
    """Measure each original logical qubit into its index in the output register."""
    physical_qubits = range(num_qubits) if circuit.layout is None else circuit.layout.final_index_layout()
    register = ClassicalRegister(num_qubits, "meas")
    circuit.add_register(register)
    circuit.measure(physical_qubits, register)
