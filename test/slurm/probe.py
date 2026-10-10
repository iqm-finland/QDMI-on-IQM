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

from provider_probe import open_device_from_license  # ty: ignore[unresolved-import]
from qiskit import QuantumCircuit, transpile

from iqm.qdmi.qiskit import IQMBackend


def main() -> None:
    """Retrieve eight shots from the licensed Emerald Resonance mock."""
    device = open_device_from_license()

    backend = IQMBackend(device=device)
    circuit = QuantumCircuit(2)
    circuit.h(0)
    circuit.cx(0, 1)
    circuit.measure_all()
    circuit = transpile(circuit, backend)
    counts = backend.run(circuit, shots=8).result().get_counts()
    assert sum(counts.values()) == 8


if __name__ == "__main__":
    main()
