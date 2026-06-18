#!/usr/bin/env python3
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

"""Simple Python script to run a real workflow on Resonance for SPANK validation."""

from __future__ import annotations

from qiskit import QuantumCircuit, transpile

from iqm.qdmi.qiskit import IQMBackend


def main() -> None:
    """Run a Bell state circuit on Resonance."""
    # Initialize backend
    backend = IQMBackend()

    # Build a simple Bell state circuit
    qc = QuantumCircuit(2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure_all()

    # Transpile the circuit
    transpiled_qc = transpile(qc, backend)

    # Submit job
    shots = 8
    job = backend.run(transpiled_qc, shots=shots)

    # Wait for results
    result = job.result()
    counts = result.get_counts()

    total_shots = sum(counts.values())

    assert total_shots == shots


if __name__ == "__main__":
    main()
