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

import logging
import os
from contextlib import contextmanager
from math import ceil
from time import monotonic, sleep
from typing import TYPE_CHECKING

from mqt.core.plugins.qiskit.backend import QDMIBackend
from qiskit import ClassicalRegister

from iqm.qdmi.iteration import IterationClient, IterationError
from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    import argparse
    from collections.abc import Generator

    from qiskit import QuantumCircuit

log = logging.getLogger(__name__)
_TERMINAL_STATES = frozenset({"completed", "cancelled", "failed"})


def add_backend_arguments(parser: argparse.ArgumentParser) -> None:
    """Add the common showcase backend and iteration flags."""
    parser.add_argument("--backend", choices=("iqm", "sim"), default="iqm")
    parser.add_argument("--iteration-mode", action="store_true", help="Reserve the IQM quantum computer for this run.")


def validate_backend_arguments(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """Reject simulator reservations before preparing the workload."""
    if args.iteration_mode and args.backend == "sim":
        parser.error("--iteration-mode requires --backend iqm")


def _wait_for_state(
    client: IterationClient, iteration_id: str, state: str | None, states: frozenset[str], deadline: float
) -> str | None:
    while state not in states:
        sleep(min(2, max(0, deadline - monotonic())))
        timeout_ms = min(30_000, ceil((deadline - monotonic()) * 1000))
        if timeout_ms <= 0:
            msg = f"Timed out waiting for iteration {iteration_id} (last state {state})"
            raise TimeoutError(msg)
        state = client.get(iteration_id, timeout_ms=timeout_ms).state
    return state


def _wait_for_activation(client: IterationClient, iteration_id: str, initial_state: str | None) -> None:
    state = _wait_for_state(client, iteration_id, initial_state, _TERMINAL_STATES | {"active"}, monotonic() + 300)
    if state != "active":
        msg = f"IQM iteration {iteration_id} ended in state {state} before activation"
        raise RuntimeError(msg)


def _end_iteration(client: IterationClient, iteration_id: str, workload_error: BaseException | None = None) -> None:
    deadline = monotonic() + 330
    try:
        finished = client.end(iteration_id)
        state = _wait_for_state(client, iteration_id, finished.state, _TERMINAL_STATES, deadline)
    except BaseException:
        msg = f"Could not confirm termination of IQM iteration {iteration_id}. Keep this ID for recovery."
        log.exception(msg)
        if workload_error is None:
            raise
        workload_error.add_note(msg)
    else:
        log.info("IQM iteration %s ended in state %s", iteration_id, state)


@contextmanager
def showcase_backend(backend: str, *, num_qubits: int, iteration_mode: bool) -> Generator[QDMIBackend, None, None]:
    """Open the showcase backend for the duration of the quantum workload.

    Yields:
        The backend, attached to a reservation when requested.

    Raises:
        ValueError: Required IQM routing settings are missing.
        IterationError: The server could not confirm a usable reservation.
        KeyboardInterrupt: The user interrupted execution.
    """
    if backend == "sim":
        yield QDMIBackend.from_device_id("mqt.ddsim.default", custom1=str(num_qubits))
        return
    if not iteration_mode:
        yield IQMBackend()
        return

    base_url = os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL")
    qc_id = os.getenv("IQM_QC_ID") or None
    qc_alias = None if qc_id else (os.getenv("IQM_QUANTUM_COMPUTER") or os.getenv("IQM_QC_ALIAS"))
    qc = qc_id or qc_alias
    if not base_url or not qc:
        msg = "Iteration mode requires an IQM server URL and a quantum computer ID or alias"
        raise ValueError(msg)

    with IterationClient(base_url=base_url) as client:
        try:
            reservation = client.create(qc, 900, idle_timeout_seconds=120)
        except (IterationError, KeyboardInterrupt) as error:
            partial = error.result if isinstance(error, IterationError) else error.__dict__.get("result")
            if partial is not None and partial.iteration_id is not None:
                log.info("The server confirmed iteration %s", partial.iteration_id)
                _end_iteration(client, partial.iteration_id, error)
            elif isinstance(error, IterationError) and error.diagnostics.get("creation_uncertain"):
                log.warning("Check the server's iteration listing before requesting another reservation.")
            raise

        iteration_id = reservation.iteration_id
        assert iteration_id is not None
        try:
            log.info("Requested IQM iteration %s", iteration_id)
            _wait_for_activation(client, iteration_id, reservation.state)
            yield IQMBackend(base_url=base_url, qc_id=qc_id, qc_alias=qc_alias, iteration_id=iteration_id)
        except BaseException as error:
            _end_iteration(client, iteration_id, error)
            raise
        else:
            _end_iteration(client, iteration_id)


def measure_logical_qubits(circuit: QuantumCircuit, num_qubits: int) -> None:
    """Measure each original logical qubit into its index in the output register."""
    physical_qubits = range(num_qubits) if circuit.layout is None else circuit.layout.final_index_layout()
    register = ClassicalRegister(num_qubits, "meas")
    circuit.add_register(register)
    circuit.measure(physical_qubits, register)
