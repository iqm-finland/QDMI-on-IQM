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
import os
from collections.abc import Mapping
from pathlib import Path
from typing import TYPE_CHECKING, ClassVar
from uuid import UUID

try:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from mqt.core.plugins.qiskit.exceptions import CircuitValidationError, UnsupportedDeviceError
    from mqt.core.qdmi import CustomProperty
    from mqt.core.qdmi.builtin_driver import open_device
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from qiskit.transpiler import Target

from . import IQM_QDMI_DEVICE_ID
from .gates import MoveGate

if TYPE_CHECKING:
    from mqt.core.plugins.qiskit.provider import QDMIProvider
    from mqt.core.qdmi import Device
    from mqt.core.typing import QDMIJobParameters
    from qiskit.circuit import Instruction
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

    #: MOVE is native to IQM's star-topology devices but absent from Qiskit's
    #: standard gate library, so the Target needs it supplied here.
    _EXTRA_GATES: ClassVar[dict[str, Instruction | type[Instruction]]] = {"move": MoveGate()}

    @property
    def physical_qubits(self) -> tuple[int, ...]:
        """Device site indices in target order, including computational resonators."""
        return self._physical_qubits

    def _build_target(self) -> Target:
        """Build a target from calibrated qubits and computational resonators.

        Returns:
            A calibrated target with contiguous indices and native resonator operations.

        Raises:
            UnsupportedDeviceError: No qubits have both required calibrations.
        """
        target = super()._build_target()
        # The IQM device lists qubits first, followed by computational resonators.
        num_qubits = self.device.qubits_num()
        self._physical_qubits = tuple(
            index
            for index in range(target.num_qubits)
            if index >= num_qubits
            or all(target.instruction_supported(operation_name=gate, qargs=(index,)) for gate in ("r", "measure"))
        )
        if not any(index < num_qubits for index in self._physical_qubits):
            msg = "No IQM qubits have calibrated PRX and measurement operations."
            raise UnsupportedDeviceError(msg)
        if len(self._physical_qubits) == target.num_qubits:
            return target

        indices = {physical: logical for logical, physical in enumerate(self._physical_qubits)}
        restricted = Target(description=target.description, num_qubits=len(indices))
        # IQM native operations have explicit calibrated loci.
        for name, placements in target.items():
            properties = {
                tuple(indices[index] for index in locus): props
                for locus, props in placements.items()
                if locus is not None and all(index in indices for index in locus)
            }
            if properties:
                restricted.add_instruction(target.operation_from_name(name), properties, name=name)
        return restricted

    @classmethod
    def _default_options(cls) -> Options:
        """Return shot options and optional IQM run-request fields.

        Returns:
            Backend defaults; ``None`` leaves server options at their defaults.
        """
        options = super()._default_options()
        options.update_options(run_request_options=None)
        return options

    def _job_parameters(self, options: Mapping[str, object]) -> QDMIJobParameters:  # ruff:ignore[no-self-use]
        """Serialize the run-request mapping into QDMI ``custom1``.

        Returns:
            Custom job parameters shared by every circuit in the run.

        Raises:
            CircuitValidationError: The options contain reserved fields,
                invalid JSON values, or shot-discarding heralding.
        """
        request_options = options.get("run_request_options")
        if request_options is None:
            return {}
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
        return {"custom1": payload}

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
            calibration_id = str(UUID(str(calibration_set_id))) if calibration_set_id is not None else None
            device_id = device_id or IQM_QDMI_DEVICE_ID
            if device_id == IQM_QDMI_DEVICE_ID:
                base_url = base_url or os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL")
                if not qc_id and not qc_alias:
                    qc_id = os.getenv("IQM_QC_ID")
                    qc_alias = os.getenv("IQM_QUANTUM_COMPUTER") or os.getenv("IQM_QC_ALIAS")
            tokens_file_value = tokens_file or os.getenv("IQM_TOKENS_FILE")
            device = open_device(
                device_id,
                base_url=base_url or None,
                token=token or os.getenv("IQM_TOKEN"),
                auth_file=Path(tokens_file_value) if tokens_file_value else None,
                custom1=qc_id,
                custom2=qc_alias,
                custom4=calibration_id,
            )
        super().__init__(device=device, provider=provider, device_id=device_id)

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
