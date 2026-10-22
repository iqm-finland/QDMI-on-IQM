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

"""Run the Qiskit adapter's eight-shot test in a Slurm allocation."""

from __future__ import annotations

import os
from pathlib import Path

from mqt.core.qdmi import builtin_driver as qdmi_driver
from qiskit.circuit import QuantumCircuit
from qiskit.compiler import transpile

from iqm.qdmi import IQM_QDMI_LIBRARY_PATH
from iqm.qdmi.qiskit import IQMBackend

# This workload requests one local device license with a count of one.
device_id = os.environ["SLURM_JOB_LICENSES"].removesuffix(":1")
device = qdmi_driver.open_device(device_id)
assert str(IQM_QDMI_LIBRARY_PATH.resolve()) in Path("/proc/self/maps").read_text(encoding="utf-8")

backend = IQMBackend(device=device)
circuit = QuantumCircuit(2)
circuit.h(0)
circuit.cx(0, 1)
circuit.measure_all()
transpiled_circuit = transpile(circuit, backend=backend)
job = backend.run(transpiled_circuit, shots=8)
counts = job.result().get_counts()
assert sum(counts.values()) == 8
