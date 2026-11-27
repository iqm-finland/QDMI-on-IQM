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

"""Qiskit-facing integration for the IQM QDMI device library."""

from __future__ import annotations

import json
from collections.abc import Generator, Mapping
from contextlib import contextmanager
from copy import copy
from typing import TYPE_CHECKING
from uuid import UUID

try:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from mqt.core.plugins.qiskit.exceptions import CircuitValidationError
    from mqt.core.qdmi import CustomProperty
    from mqt.core.qdmi.builtin_driver import open_device
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from . import IQM_QDMI_DEVICE_ID
from ._session import session_parameters
from .iteration import reservation

if TYPE_CHECKING:
    import os

    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from mqt.core.qdmi import Device
    from mqt.core.typing import QDMIJobParameters
    from qiskit.providers import Options

__all__ = ["IQMBackend"]


def __dir__() -> list[str]:
    return __all__


class IQMBackend(QDMIBackend):
    """Qiskit backend for the packaged IQM QDMI device library.

    This backend loads the shared library distributed with `iqm-qdmi` and
    exposes it through MQT Core's Qiskit-compatible QDMI backend.

    Args:
        device_id: Stable ID from the installed catalogue. Defaults to `iqm.default`.
        device: An already-open device.
        provider: Optional Qiskit provider to associate with this backend.
        base_url: Base URL of the IQM service. Overrides `IQM_SERVER_URL`, its
            `IQM_BASE_URL` alias, and the manifest default when provided.
        token: Authentication token. Defaults to `IQM_TOKEN`.
        tokens_file: Path to an authentication file. Defaults to `IQM_TOKENS_FILE`.
        qc_id: Optional IQM quantum computer identifier. Defaults to `IQM_QC_ID`.
        qc_alias: Optional IQM quantum computer alias. Defaults to
            `IQM_QUANTUM_COMPUTER`, then its `IQM_QC_ALIAS` alias.
        calibration_set_id: Optional calibration UUID to use for target construction
            and execution. When omitted, resolve the server default at initialization.

    Environment defaults for the endpoint and quantum computer apply only to
    `iqm.default`. An explicit ID or alias takes precedence over either selector
    from the environment. Named presets use their manifest configuration.
    """

    @classmethod
    def _default_options(cls) -> Options:
        """Return shot options and optional IQM run-request fields.

        Returns:
            Backend defaults; ``None`` leaves server options at their defaults.
        """
        options = super()._default_options()
        options.update_options(memory=True, run_request_options=None, reservation_id=None)
        return options

    def _job_parameters(self, options: Mapping[str, object]) -> QDMIJobParameters:  # ruff:ignore[no-self-use]
        """Encode the IQM run-request options and reservation ID as QDMI job parameters.

        Returns:
            Custom job parameters shared by every circuit in the run.

        Raises:
            CircuitValidationError: The options contain reserved fields,
                invalid JSON values, or shot-discarding heralding.
        """
        parameters: QDMIJobParameters = {}
        if (reservation_id := options.get("reservation_id")) is not None:
            try:
                parameters["custom2"] = str(UUID(str(reservation_id)))
            except ValueError as exc:
                msg = "'reservation_id' must be a UUID"
                raise CircuitValidationError(msg) from exc
        request_options = options.get("run_request_options")
        if request_options is None:
            return parameters
        if not isinstance(request_options, Mapping):
            msg = "'run_request_options' must be a JSON object"
            raise CircuitValidationError(msg)
        if reserved := request_options.keys() & {"circuits", "shots", "calibration_set_id"}:
            msg = f"'run_request_options' cannot override {', '.join(sorted(reserved))}"
            raise CircuitValidationError(msg)
        try:
            payload = json.dumps(dict(request_options), allow_nan=False)
        except (TypeError, ValueError, OverflowError, RecursionError) as exc:
            msg = "'run_request_options' must contain finite JSON-compatible values"
            raise CircuitValidationError(msg) from exc
        if request_options.get("heralding_mode") == "zeros":
            msg = "IQM heralding_mode='zeros' is unsupported because it may discard shots."
            raise CircuitValidationError(msg)
        parameters["custom1"] = payload
        return parameters

    def __init__(
        self,
        *,
        device_id: str | None = None,
        device: Device | None = None,
        provider: QDMIProvider | None = None,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
        calibration_set_id: str | UUID | None = None,
    ) -> None:
        """Initialize the IQM Qiskit backend.

        Raises:
            ValueError: If an already-open device is combined with session overrides.
        """
        if device is not None:
            if any(value is not None for value in (base_url, token, tokens_file, qc_id, qc_alias, calibration_set_id)):
                msg = "An already-open device cannot be combined with session overrides."
                raise ValueError(msg)
        else:
            device_id = device_id or IQM_QDMI_DEVICE_ID
            device = open_device(
                device_id,
                **session_parameters(
                    device_id,
                    base_url=base_url,
                    token=token,
                    tokens_file=tokens_file,
                    qc_id=qc_id,
                    qc_alias=qc_alias,
                    calibration_set_id=calibration_set_id,
                ),
            )
        super().__init__(device=device, provider=provider, device_id=device_id)

    @contextmanager
    def reservation(
        self, *, max_duration_seconds: int, idle_timeout_seconds: int | None = None
    ) -> Generator[IQMBackend, None, None]:
        """Reserve the quantum computer using this backend's open device.

        Args:
            max_duration_seconds: Maximum active duration, from 1 to 2700 seconds.
            idle_timeout_seconds: Idle limit in seconds, or zero or None for the server default.

        Yields:
            A backend for runs and primitives inside the context. It retains the
            reservation ID after exit. Use the original backend for later jobs.
        """
        with reservation(
            self.device, max_duration_seconds=max_duration_seconds, idle_timeout_seconds=idle_timeout_seconds
        ) as reservation_id:
            reserved = copy(self)
            # BackendV2 exposes options read-only. Give the copy its own defaults.
            reserved._options = copy(self.options)  # ruff:ignore[private-member-access]
            reserved.set_options(reservation_id=reservation_id)
            reserved.last_job = None
            yield reserved

    @property
    def calibration_set_id(self) -> str:
        """Effective calibration UUID to share with execution clients.

        Raises:
            RuntimeError: If the device does not report a calibration UUID.
        """
        calibration_id = self.device.query_custom_property(CustomProperty.CUSTOM1, str)
        if calibration_id is None:
            msg = "The IQM device did not report its calibration set ID."
            raise RuntimeError(msg)
        return calibration_id
