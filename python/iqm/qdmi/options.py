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

"""Encode optional IQM run-request fields for the QDMI device."""

from __future__ import annotations

import json
from collections.abc import Mapping
from typing import TYPE_CHECKING

from mqt.core.plugins.qiskit.exceptions import CircuitValidationError

if TYPE_CHECKING:
    from mqt.core.typing import QDMIJobParameters

_RESERVED_FIELDS = {"circuits", "shots", "calibration_set_id"}


def execution_parameters(options: Mapping[str, object]) -> QDMIJobParameters:
    """Encode optional IQM run-request fields as one custom job parameter.

    Args:
        options: Effective IQM backend options for one run.

    Returns:
        A JSON object in ``custom1``, or no custom value when unset.

    Raises:
        CircuitValidationError: An option cannot be represented as JSON or
            tries to replace a QDMI-owned request field.
    """
    if unknown := options.keys() - {"run_request_options"}:
        msg = f"Unsupported execution options: {', '.join(sorted(unknown))}"
        raise CircuitValidationError(msg)
    request_options = options.get("run_request_options")
    if request_options is None:
        return {"custom1": None}
    if not isinstance(request_options, Mapping):
        msg = "'run_request_options' must be a JSON object"
        raise CircuitValidationError(msg)
    if reserved := request_options.keys() & _RESERVED_FIELDS:
        msg = f"'run_request_options' cannot override {', '.join(sorted(reserved))}"
        raise CircuitValidationError(msg)
    pending = [request_options]
    seen: set[int] = set()
    while pending:
        value = pending.pop()
        if id(value) in seen:
            continue
        seen.add(id(value))
        if isinstance(value, Mapping):
            if any(not isinstance(key, str) for key in value):
                msg = "'run_request_options' object keys must be strings"
                raise CircuitValidationError(msg)
            pending.extend(value.values())
        elif isinstance(value, (list, tuple)):
            pending.extend(value)
    try:
        payload = json.dumps(dict(request_options), allow_nan=False)
    except (TypeError, ValueError, OverflowError, RecursionError) as exc:
        msg = "'run_request_options' must contain finite JSON-compatible values"
        raise CircuitValidationError(msg) from exc
    return {"custom1": payload}
