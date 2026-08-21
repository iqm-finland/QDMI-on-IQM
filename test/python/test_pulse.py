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
from typing import TYPE_CHECKING, Any

import numpy as np
import pytest

pytest.importorskip("iqm.pulla", reason="requires the `pulla` extra")

from exa.common.data.setting_node import SettingNode
from iqm.models.playlist.playlist import Playlist
from iqm.pulla.pulla import PullaStash
from iqm.pulse.playlist.instructions import ReadoutMetrics
from iqm.station_control.client.serializers import (
    serialize_run_definition,
    serialize_sweep_results,
)
from iqm.station_control.interface.models import RunDefinition, SweepDefinition

from iqm.qdmi import pulse

if TYPE_CHECKING:
    from collections.abc import Sequence

    from iqm.pulse import Circuit

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


class FakeSettings:
    """Records the shot count the compilation asks for."""

    def __init__(self) -> None:
        """Start out without a shot count."""
        self.shots: int | None = None

    def set_shots(self, shots: int) -> None:
        """Record the requested shot count."""
        self.shots = shots


class FakeCompiler:
    """Stands in for IQM's circuit-to-pulse compiler."""

    def __init__(self) -> None:
        """Start out without settings."""
        self.settings = FakeSettings()

    def get_settings(self, circuits: Sequence[Circuit] | None = None) -> FakeSettings:
        """Hand out the settings this compilation will use.

        Returns:
            The recording settings object.
        """
        del circuits
        return self.settings

    @staticmethod
    def compile(
        circuits: Sequence[Circuit], settings: FakeSettings | None = None
    ) -> tuple[RunDefinition, dict[str, Any]]:
        """Pretend to compile the circuits.

        Returns:
            A run definition carrying placeholder identifiers, and a context.
        """
        del circuits
        return make_run_definition(), {"settings": settings}


class FakePulla:
    """Stands in for a connection to an IQM Server."""

    def __init__(
        self,
        iqm_server_url: str | None = None,
        *,
        quantum_computer: str | None = None,
        token: str | None = None,
        tokens_file: str | None = None,
    ) -> None:
        """Record the connection parameters."""
        self.iqm_server_url = iqm_server_url
        self.quantum_computer = quantum_computer
        self.token = token
        self.tokens_file = tokens_file
        self.compiler = FakeCompiler()
        self.calibration_set_id: str | None = None

    def get_calibration_stash(self, calibration_set_id: str = "default") -> PullaStash:
        """Record which calibration set was asked for.

        Returns:
            An empty stash.
        """
        self.calibration_set_id = calibration_set_id
        return PullaStash({})

    def get_standard_compiler(self, loading_rules: list[object] | None = None) -> FakeCompiler:
        """Hand out the fake compiler.

        Returns:
            The fake compiler.
        """
        del loading_rules
        return self.compiler


def test_compiling_stamps_fresh_identifiers_onto_the_run(monkeypatch: pytest.MonkeyPatch) -> None:
    """The client owns the run and sweep identifiers, so compiling must set them.

    Nothing else does: the device forwards the payload without reading it, and
    the IQM Server rejects a run whose identifiers repeat an earlier one.
    """
    monkeypatch.setattr(pulse, "Pulla", FakePulla)

    program = pulse.compile_pulse_program([], base_url="https://example.invalid", shots=13)

    assert program.run_definition.run_id != uuid.UUID(int=0)
    assert program.run_definition.sweep_definition.sweep_id != uuid.UUID(int=0)
    assert program.payload == serialize_run_definition(program.run_definition).SerializeToString()


def test_compiling_passes_the_shot_count_and_calibration_set_through(monkeypatch: pytest.MonkeyPatch) -> None:
    """Both reach the compiler rather than being silently dropped."""
    created: list[FakePulla] = []

    def record(*args: object, **kwargs: object) -> FakePulla:
        created.append(FakePulla(*args, **kwargs))  # ty: ignore[invalid-argument-type]
        return created[-1]

    monkeypatch.setattr(pulse, "Pulla", record)

    pulse.compile_pulse_program([], base_url="https://example.invalid", shots=64, calibration_set_id="cal-1")

    assert created[0].compiler.settings.shots == 64
    assert created[0].calibration_set_id == "cal-1"


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
