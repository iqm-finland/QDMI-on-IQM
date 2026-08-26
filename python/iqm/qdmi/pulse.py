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

"""Pulse-level programs for the IQM QDMI device.

QDMI has no program format of its own for pulse-level programs, so the device
carries one in custom program format 1: a serialized IQM `RunDefinition`
protobuf, handed to the IQM Server byte for byte. This module compiles quantum
circuits into that payload and decodes the raw `sweep_results` artifact the
device returns as custom job result 2.

Compiling and decoding both need IQM's circuit-to-pulse compiler, so this
module requires the `pulla` extra. The device itself does not: it forwards the
payload without inspecting it, which is why a program produced elsewhere works
just as well.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import TYPE_CHECKING, Any

try:
    from iqm.cpc.compiler.post_process import (
        CircuitExecutionResults,
        PullaData,
        construct_circuit_execution_results,
    )
    from iqm.station_control.client.serializers import deserialize_sweep_results, serialize_run_definition
    from iqm.station_control.interface.models import JobExecutorStatus, RunData, RunDefinition, SweepData
except ImportError as e:
    msg = (
        "Failed to import IQM Pulla. "
        "Ensure that `iqm-qdmi` is installed with the `pulla` extra, e.g., via `uv pip install iqm-qdmi[pulla]`."
    )
    raise ImportError(msg) from e

if TYPE_CHECKING:
    from collections.abc import Sequence

    from iqm.cpc.compiler.compiler import Compiler
    from iqm.pulse import Circuit

__all__ = ["PulseProgram", "compile_pulse_program", "decode_sweep_results"]

DEFAULT_SHOTS = 1024
"""Number of repetitions used when the caller does not ask for a specific count."""


def __dir__() -> list[str]:
    return __all__


@dataclass(frozen=True)
class PulseProgram:
    """A compiled pulse-level program, ready to submit through QDMI.

    Decoding the results needs more than the payload, so the run definition and
    the compiler context that produced it travel along.
    """

    payload: bytes
    """Serialized `RunDefinition` protobuf, to be set as the QDMI program."""

    run_definition: RunDefinition
    """The run definition the payload was serialized from."""

    context: dict[str, Any]
    """Final compiler context, which records how to read the results back."""


def compile_pulse_program(
    compiler: Compiler,
    circuits: Sequence[Circuit],
    *,
    shots: int = DEFAULT_SHOTS,
) -> PulseProgram:
    """Compile circuits with a caller-owned IQM pulse compiler.

    Args:
        compiler: Reusable compiler configured by the caller's Pulla instance.
        circuits: Circuits to compile.
        shots: Number of repetitions of each circuit.

    Returns:
        The compiled program.
    """
    settings = compiler.get_settings(circuits=circuits)
    settings.set_shots(shots)
    run_definition, context = compiler.compile(circuits, settings=settings)

    # The IQM Server expects the client to identify the run and its sweep, which
    # is what `Pulla.submit_playlist` would otherwise do on submission.
    run_definition.run_id = uuid.uuid4()
    run_definition.sweep_definition.sweep_id = uuid.uuid4()

    return PulseProgram(
        payload=serialize_run_definition(run_definition).SerializeToString(),
        run_definition=run_definition,
        context=context,
    )


def decode_sweep_results(program: PulseProgram, sweep_results: bytes) -> CircuitExecutionResults:
    """Turn the raw `sweep_results` artifact into per-circuit measurement results.

    Args:
        program: The program that produced the results.
        sweep_results: The artifact exactly as the device returned it.

    Returns:
        The measurement results, keyed by the circuits' own measurement keys.
    """
    run_definition = program.run_definition
    sweep_definition = run_definition.sweep_definition
    # Post-processing reads the run metadata alongside the results. The
    # timestamps are not part of that, so they are filled in as of now.
    now = datetime.now(timezone.utc)
    run_data = RunData(
        run_id=run_definition.run_id,
        username=run_definition.username,
        experiment_name=run_definition.experiment_name,
        experiment_label=run_definition.experiment_label,
        options=run_definition.options,
        additional_run_properties=run_definition.additional_run_properties,
        software_version_set_id=run_definition.software_version_set_id,
        hard_sweeps=run_definition.hard_sweeps,
        components=run_definition.components,
        default_data_parameters=run_definition.default_data_parameters,
        default_sweep_parameters=run_definition.default_sweep_parameters,
        sweep_data=SweepData(
            sweep_id=sweep_definition.sweep_id,
            dut_label=sweep_definition.dut_label,
            settings=sweep_definition.settings,
            sweeps=sweep_definition.sweeps,
            return_parameters=sweep_definition.return_parameters,
            created_timestamp=now,
            modified_timestamp=now,
            begin_timestamp=now,
            end_timestamp=now,
            job_status=JobExecutorStatus.READY,
        ),
        created_timestamp=now,
        modified_timestamp=now,
        begin_timestamp=now,
        end_timestamp=now,
    )
    pulla_data = PullaData(sweep_results=deserialize_sweep_results(sweep_results), run_data=run_data)
    return construct_circuit_execution_results(pulla_data, program.context)
