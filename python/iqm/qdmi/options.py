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

"""Validation and encoding for IQM Qiskit execution options."""

from __future__ import annotations

import json
import math
import struct
from collections.abc import Mapping
from numbers import Integral, Real
from typing import TYPE_CHECKING

from mqt.core.plugins.qiskit.exceptions import CircuitValidationError

if TYPE_CHECKING:
    from mqt.core.typing import QDMIJobParameters

_ENUM_OPTIONS = {
    "heralding_mode": ("custom1", {"none", "zeros"}),
    "move_gate_validation": ("custom2", {"strict", "allow_prx", "none"}),
    "move_gate_frame_tracking": ("custom3", {"full", "no_detuning_correction", "none"}),
    "dd_mode": ("custom4", {"disabled", "enabled"}),
}


def execution_parameters(options: Mapping[str, object]) -> QDMIJobParameters:
    """Validate effective options and encode IQM custom job parameters.

    Args:
        options: Backend defaults combined with per-run overrides.

    Returns:
        Custom parameters; absent or ``None`` settings retain native defaults.

    Raises:
        CircuitValidationError: An option has an unsupported name, type, or value.
    """
    supported = {*_ENUM_OPTIONS, "qubit_mapping", "max_circuit_duration_over_t2", "active_reset_cycles", "dd_strategy"}
    if unknown := options.keys() - supported - {"shots", "memory"}:
        msg = f"Unsupported execution options: {', '.join(sorted(unknown))}"
        raise CircuitValidationError(msg)
    values: dict[str, str] = {}
    for name, (slot, choices) in _ENUM_OPTIONS.items():
        value = options.get(name)
        if value is not None:
            if not isinstance(value, str) or value not in choices:
                msg = f"Invalid '{name}': expected one of {sorted(choices)}"
                raise CircuitValidationError(msg)
            values[slot] = value

    mapping = options.get("qubit_mapping")
    if mapping is not None:
        if not isinstance(mapping, Mapping) or not all(
            isinstance(name, str) and name and not any(char in name for char in ",:\0")
            for pair in mapping.items()
            for name in pair
        ):
            msg = "'qubit_mapping' must map nonempty names without commas, colons, or NUL characters"
            raise CircuitValidationError(msg)
        values["custom5"] = ",".join(f"{logical}:{physical}" for logical, physical in mapping.items())

    extra: dict[str, object] = {}
    duration = options.get("max_circuit_duration_over_t2")
    if duration is not None:
        msg = "'max_circuit_duration_over_t2' must be a finite positive number"
        if not isinstance(duration, Real) or isinstance(duration, bool):
            raise CircuitValidationError(msg)
        try:
            number = float(duration)
        except OverflowError as exc:
            raise CircuitValidationError(msg) from exc
        if not math.isfinite(number) or number <= 0:
            raise CircuitValidationError(msg)
        extra["max_circuit_duration_over_t2"] = number
    cycles = options.get("active_reset_cycles")
    if cycles is not None:
        if (
            not isinstance(cycles, Integral)
            or isinstance(cycles, bool)
            or not 0 <= int(cycles) < 2 ** (8 * struct.calcsize("P"))
        ):
            msg = "'active_reset_cycles' must be a nonnegative integer representable as size_t"
            raise CircuitValidationError(msg)
        extra["active_reset_cycles"] = int(cycles)
    strategy = options.get("dd_strategy")
    if strategy is not None:
        if not isinstance(strategy, dict):
            msg = "'dd_strategy' must be a JSON object"
            raise CircuitValidationError(msg)
        extra["dd_strategy"] = strategy
    if extra:
        if "custom4" in values:
            extra["dd_mode"] = values["custom4"]
        try:
            values["custom4"] = json.dumps({"iqm_execution_options": 1, **extra}, allow_nan=False)
        except (TypeError, ValueError, OverflowError, RecursionError) as exc:
            msg = "IQM execution options must contain finite JSON-compatible values"
            raise CircuitValidationError(msg) from exc
    # Serialization already rejected cycles. Check keys recursively because
    # json.dumps otherwise silently converts numeric keys to strings.
    pending = [strategy]
    while pending:
        value = pending.pop()
        if isinstance(value, dict):
            if any(not isinstance(key, str) for key in value):
                msg = "'dd_strategy' object keys must be strings"
                raise CircuitValidationError(msg)
            pending.extend(value.values())
        elif isinstance(value, (list, tuple)):
            pending.extend(value)
    return {
        "custom1": values.get("custom1"),
        "custom2": values.get("custom2"),
        "custom3": values.get("custom3"),
        "custom4": values.get("custom4"),
        "custom5": values.get("custom5"),
    }
