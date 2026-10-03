# Copyright (c) 2026 IQM Finland Oy
# All rights reserved.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

"""Submit an IQM circuit through the license-selected Qiskit adapter."""

from __future__ import annotations

import os
from unittest import TestCase
from unittest.mock import patch

from mqt.core.plugins.pennylane import PennyLaneUnsupportedFormatError, QDMIDevice
from provider_probe import open_device_from_license  # ty: ignore[unresolved-import]
from qiskit import QuantumCircuit, transpile

from iqm.qdmi.qiskit import IQMBackend


def main() -> None:
    """Reuse the licensed IQM handle and retrieve deterministic fixture shots."""
    with patch.dict(os.environ, {"IQM_QC_ID": "qc-default", "IQM_QC_ALIAS": "default"}):
        device = open_device_from_license()
    assert device.qubits_num() == 2

    backend = IQMBackend(device=device)
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure_all()
    circuit = transpile(circuit, backend)
    counts = backend.run(circuit, shots=8).result().get_counts()
    assert counts == {"00": 4, "11": 4}

    # The shared workload image runs this script without pytest.
    with TestCase().assertRaisesRegex(  # ruff: ignore[pytest-unittest-raises-assertion]
        PennyLaneUnsupportedFormatError, "neither OpenQASM 3 nor OpenQASM 2"
    ):
        QDMIDevice(device=device, wires=2)
    with (
        patch.dict(os.environ, {"IQM_TOKENS_FILE": "/opt/missing-fixture-tokens.json"}),
        TestCase().assertRaises(RuntimeError),  # ruff: ignore[pytest-unittest-raises-assertion]
    ):
        open_device_from_license()


if __name__ == "__main__":
    main()
