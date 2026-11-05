#!/usr/bin/env -S uv run --script --quiet
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

# /// script
# requires-python = ">=3.11,<3.15"
# dependencies = ["iqm-qdmi[pulla]"]
# [tool.uv.sources]
# iqm-qdmi = { path = ".." }
# ///

"""Compile and execute a pulse program with a caller-owned IQM compiler."""

from __future__ import annotations

import argparse
from uuid import uuid4

from iqm.iqm_client import IQMClient
from iqm.pulse import Circuit, CircuitOperation
from iqm.station_control.client.serializers import deserialize_sweep_results, serialize_run_definition
from mqt.core.qdmi import CustomProperty, ProgramFormat, builtin_driver

from iqm.qdmi import IQM_QDMI_DEVICE_ID


def main() -> None:
    """Measure one physical qubit through the pulse-level QDMI interface.

    Raises:
        TimeoutError: The pulse job does not complete within ten minutes.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qubit", default="QB1", help="Physical qubit to measure (default: QB1)")
    parser.add_argument("--shots", type=int, default=1, help="Repetitions encoded in the program (default: 1)")
    args = parser.parse_args()

    # Both clients use IQM_SERVER_URL, IQM_QUANTUM_COMPUTER, and IQM_TOKEN.
    client = IQMClient.from_url()
    compiler = client.get_standard_compiler()
    circuits = [Circuit("measure", (CircuitOperation("measure", (args.qubit,), {"key": "m"}),))]
    settings = compiler.get_settings(circuits=circuits)
    settings.set_shots(args.shots)
    run_definition, _context = compiler.compile(circuits, settings=settings)
    run_definition.run_id = uuid4()
    run_definition.sweep_definition.sweep_id = uuid4()
    payload = serialize_run_definition(run_definition).SerializeToString()

    device = builtin_driver.open_device(IQM_QDMI_DEVICE_ID)
    job = device.submit_job(payload, ProgramFormat.CUSTOM1)
    print(f"Submitted pulse job {job.id}")
    if not job.wait(timeout=600):
        job.cancel()
        msg = "Pulse job did not finish within 10 minutes"
        raise TimeoutError(msg)

    artifact = job.get_custom_result(CustomProperty.CUSTOM2, bytes)
    assert artifact is not None
    for label, sweep_spots in deserialize_sweep_results(artifact).items():
        print(f"{label}: {sweep_spots}")


if __name__ == "__main__":
    main()
