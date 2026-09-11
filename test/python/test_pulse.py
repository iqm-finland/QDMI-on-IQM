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

"""Tests for compiling and decoding pulse-level programs.

These need the `pulla` extra, which supports a narrower range of Python
versions than this package, so the whole module skips where it is absent.
Compiling for real needs a live IQM Server, so the compiler is faked here and
only the glue this package owns is exercised.
"""

from __future__ import annotations

import uuid
from typing import TYPE_CHECKING, cast
from unittest.mock import Mock

import numpy as np
import pytest

pytest.importorskip("iqm.pulla", reason="requires the `pulla` extra")

from exa.common.data.setting_node import SettingNode
from iqm.models.playlist.playlist import Playlist
from iqm.pulse.playlist.instructions import ReadoutMetrics
from iqm.station_control.client.serializers import (
    serialize_run_definition,
    serialize_sweep_results,
)
from iqm.station_control.interface.models import RunDefinition, SweepDefinition

from iqm.qdmi import pulse

if TYPE_CHECKING:
    from iqm.cpc.compiler.compiler import Compiler

READOUT_LABEL = "QB1__m"


def make_run_definition() -> RunDefinition:
    """Build the smallest run definition the serializers accept.

    Returns:
        A run definition with an empty playlist.
    """
    return RunDefinition(
        run_id=uuid.UUID(int=0),
        username="qdmi",
        experiment_name="pulse",
        experiment_label="pulse",
        sweep_definition=SweepDefinition(
            sweep_id=uuid.UUID(int=0),
            dut_label="chip",
            settings=SettingNode("root"),
            sweeps=[],
            return_parameters=[READOUT_LABEL],
            playlist=Playlist(),
        ),
    )


def test_compiling_uses_caller_owned_compiler_and_stamps_fresh_ids() -> None:
    """Compile once through the supplied compiler and prepare a unique run.

    Nothing else does: the device forwards the payload without reading it, and
    the IQM Server rejects a run whose identifiers repeat an earlier one.
    """
    compiler = Mock()
    settings = Mock()
    context = {"compiler": "context"}
    compiler.get_settings.return_value = settings
    compiler.compile.return_value = make_run_definition(), context

    program = pulse.compile_pulse_program(cast("Compiler", compiler), [], shots=13)

    compiler.get_settings.assert_called_once_with(circuits=[])
    settings.set_shots.assert_called_once_with(13)
    compiler.compile.assert_called_once_with([], settings=settings)
    assert program.run_definition.run_id != uuid.UUID(int=0)
    assert program.run_definition.sweep_definition.sweep_id != uuid.UUID(int=0)
    assert program.payload == serialize_run_definition(program.run_definition).SerializeToString()
    assert program.context is context


def test_decoding_maps_acquisition_labels_onto_measurement_keys() -> None:
    """The raw artifact names acquisitions; a caller wants its own measurement keys."""
    run_definition = make_run_definition()
    program = pulse.PulseProgram(
        payload=b"",
        run_definition=run_definition,
        context={"readout_metrics": ReadoutMetrics(num_segments=1, integration_occurrences={READOUT_LABEL: [1]})},
    )
    artifact = serialize_sweep_results(
        run_definition.sweep_definition.sweep_id, {READOUT_LABEL: [np.array([0, 1, 1, 0])]}
    )

    results = pulse.decode_sweep_results(program, artifact)

    assert results.circuit_measurement_results == [{"m": [[0], [1], [1], [0]]}]
    assert results.readout_mappings == ({"m": (READOUT_LABEL,)},)
