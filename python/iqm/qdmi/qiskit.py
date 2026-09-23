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

import os
from pathlib import Path
from typing import TYPE_CHECKING, ClassVar

try:
    from mqt.core.plugins.qiskit.backend import QDMIBackend
    from mqt.core.qdmi.driver import DeviceDefinition, open_device, register_device_if_absent
except ImportError as e:
    msg = (
        "Failed to import Qiskit plugin. "
        "Ensure that `iqm-qdmi` is installed with the `qiskit` extra, e.g., via `uv pip install iqm-qdmi[qiskit]`."
    )
    raise ImportError(msg) from e

from mqt.core.plugins.qiskit.exceptions import CircuitValidationError

from . import IQM_QDMI_DEVICE_ID, IQM_QDMI_LIBRARY_PATH, IQM_QDMI_PREFIX
from .gates import MoveGate
from .options import execution_parameters

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from typing import Any

    from mqt.core.plugins.qiskit.backend import ParametersType
    from mqt.core.plugins.qiskit.job import QDMIJob
    from mqt.core.typing import QDMIJobParameters
    from qiskit.circuit import Instruction, QuantumCircuit
    from qiskit.providers import Options

__all__ = ["IQMBackend"]

IQM_DEFAULT_BASE_URL = "https://resonance.iqm.tech"


def __dir__() -> list[str]:
    return __all__


class IQMBackend(QDMIBackend):
    """Qiskit backend for the packaged IQM QDMI device library.

    This backend loads the shared library distributed with `iqm-qdmi` and
    exposes it through MQT Core's Qiskit-compatible QDMI backend.

    Args:
        base_url: Base URL of the IQM service. Overrides `IQM_SERVER_URL`, its
            `IQM_BASE_URL` alias, and the registered device default when provided.
        token: Authentication token. Defaults to `IQM_TOKEN`.
        tokens_file: Path to an authentication file. Defaults to `IQM_TOKENS_FILE`.
        qc_id: Optional IQM quantum computer identifier. Defaults to `IQM_QC_ID`.
        qc_alias: Optional IQM quantum computer alias. Defaults to
            `IQM_QUANTUM_COMPUTER`, then its `IQM_QC_ALIAS` alias.
    """

    #: MOVE is native to IQM's star-topology devices but absent from Qiskit's
    #: standard gate library, so the Target needs it supplied here.
    _EXTRA_GATES: ClassVar[dict[str, Instruction | type[Instruction]]] = {"move": MoveGate()}

    @classmethod
    def _default_options(cls) -> Options:
        """Return shot options and optional IQM execution settings.

        Older MQT Core versions reject the new settings instead of accepting
        them without forwarding them to the device.

        Returns:
            Backend defaults; ``None`` leaves the native IQM default unchanged.
        """
        options = super()._default_options()
        if hasattr(QDMIBackend, "_job_parameters"):
            options.update_options(
                heralding_mode=None,
                move_gate_validation=None,
                move_gate_frame_tracking=None,
                dd_mode=None,
                qubit_mapping=None,
                max_circuit_duration_over_t2=None,
                active_reset_cycles=None,
                dd_strategy=None,
            )
        return options

    @staticmethod
    def _job_parameters(options: Mapping[str, object]) -> QDMIJobParameters:
        """Validate and encode IQM options for every circuit in a run.

        Returns:
            IQM custom job parameters for MQT Core's submission hook.
        """
        return execution_parameters(options)

    def run(
        self,
        run_input: QuantumCircuit | Sequence[QuantumCircuit],
        parameter_values: Sequence[ParametersType] | None = None,
        **options: Any,  # ruff:ignore[any-type]
    ) -> QDMIJob:
        """Submit circuits with validated IQM execution options.

        Returns:
            A job aggregating the submitted circuits.

        Raises:
            CircuitValidationError: An option is unknown or needs newer MQT Core.
        """
        allowed = set(self.options)
        if options.get("seed_simulator") is None:
            allowed.add("seed_simulator")
        if unsupported := options.keys() - allowed:
            msg = f"Unsupported execution options: {', '.join(sorted(unsupported))}"
            if not hasattr(QDMIBackend, "_job_parameters"):
                msg += ". IQM execution options require MQT Core with the job-option hook."
            raise CircuitValidationError(msg)
        return super().run(run_input, parameter_values=parameter_values, **options)

    def __init__(
        self,
        *,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        qc_id: str | None = None,
        qc_alias: str | None = None,
    ) -> None:
        """Initialize the IQM Qiskit backend."""
        resolved_base_url = base_url or os.getenv("IQM_SERVER_URL") or os.getenv("IQM_BASE_URL") or None
        resolved_token = token or os.getenv("IQM_TOKEN")
        tokens_file_value = tokens_file or os.getenv("IQM_TOKENS_FILE")
        resolved_tokens_file = Path(tokens_file_value) if tokens_file_value else None
        resolved_qc_id = qc_id or os.getenv("IQM_QC_ID")
        resolved_qc_alias = qc_alias or os.getenv("IQM_QUANTUM_COMPUTER") or os.getenv("IQM_QC_ALIAS")

        register_device_if_absent(
            DeviceDefinition(
                IQM_QDMI_DEVICE_ID,
                IQM_QDMI_LIBRARY_PATH,
                IQM_QDMI_PREFIX,
                base_url=IQM_DEFAULT_BASE_URL,
            )
        )
        device = open_device(
            IQM_QDMI_DEVICE_ID,
            base_url=resolved_base_url,
            token=resolved_token,
            auth_file=resolved_tokens_file,
            custom1=resolved_qc_id,
            custom2=resolved_qc_alias,
        )
        super().__init__(device=device)
