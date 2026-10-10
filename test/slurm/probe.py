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

"""Submit an IQM circuit through the Qiskit adapter."""

from __future__ import annotations

import sys
from pathlib import Path

from mqt.core.qdmi import builtin_driver
from qiskit import QuantumCircuit, transpile

from iqm.qdmi import IQM_QDMI_LIBRARY_PATH
from iqm.qdmi.qiskit import IQMBackend


def main() -> None:
    """Retrieve eight shots from the selected Emerald Resonance mock."""
    device_id = sys.argv[1]
    device = builtin_driver.open_device(device_id)
    native = Path("/opt/provider-native/lib") / IQM_QDMI_LIBRARY_PATH.name
    library = native if native.exists() else IQM_QDMI_LIBRARY_PATH
    assert str(library.resolve()) in Path("/proc/self/maps").read_text(encoding="utf-8")

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
