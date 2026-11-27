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

"""Manage IQM reservations through an open MQT Core QDMI device."""

from __future__ import annotations

import json
import logging
from contextlib import contextmanager
from typing import TYPE_CHECKING

from mqt.core.qdmi import Job, ProgramFormat

if TYPE_CHECKING:
    from collections.abc import Generator

    from mqt.core.qdmi import Device

__all__ = ["reservation"]

_LOGGER = logging.getLogger(__name__)


def _end(device: Device, job: Job, reservation_id: str) -> None:
    device.submit_job(json.dumps({"end": reservation_id}), ProgramFormat.CUSTOM2)
    # Confirm termination on the owning handle before its best-effort release.
    if not job.wait(300):
        msg = f"Timed out waiting for reservation {reservation_id} to end"
        raise TimeoutError(msg)
    if job.check() == Job.Status.FAILED:
        msg = f"Reservation {reservation_id} ended in the failed state"
        raise RuntimeError(msg)


@contextmanager
def reservation(
    device: Device, *, max_duration_seconds: int, idle_timeout_seconds: int | None = None
) -> Generator[str, None, None]:
    """Reserve the device for circuit jobs and request graceful termination on exit.

    Args:
        device: An open IQM QDMI device.
        max_duration_seconds: Maximum active duration, from 1 to 2700 seconds.
        idle_timeout_seconds: Idle limit in seconds, or zero or None for the server default.

    Yields:
        Reservation UUID to pass as the circuit job parameter ``custom2``.
        You can submit circuits as soon as the server assigns this ID.
    """
    request = {"max_duration_seconds": max_duration_seconds}
    if idle_timeout_seconds is not None:
        request["idle_timeout_seconds"] = idle_timeout_seconds
    job = device.submit_job(json.dumps(request), ProgramFormat.CUSTOM2)
    reservation_id = job.id
    workload_error = None
    try:
        _LOGGER.info("Created IQM reservation %s", reservation_id)
        yield reservation_id
    except BaseException as exc:
        workload_error = exc
        raise
    finally:
        try:
            _end(device, job, reservation_id)
        except BaseException as cleanup_error:
            if workload_error is None:
                cleanup_error.add_note(f"IQM reservation {reservation_id}")
                raise
            workload_error.add_note(f"Ending IQM reservation {reservation_id}: {cleanup_error}")
