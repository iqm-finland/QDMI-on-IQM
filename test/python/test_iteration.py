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

"""Exercise Python iteration management and Qiskit attachment with the packaged device."""

from __future__ import annotations

import json
import logging
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from threading import Thread
from typing import TYPE_CHECKING
from unittest.mock import Mock, call
from uuid import UUID

import pytest
from mqt.core.qdmi import Device, Job, ProgramFormat
from qiskit import QuantumCircuit

from iqm.qdmi.iteration import reservation
from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from collections.abc import Iterator

ITERATION_ID = "12345678-1234-1234-1234-123456789abc"
ITERATIONS = "/api/v1/iterations"
ITERATION = f"{ITERATIONS}/{ITERATION_ID}"
CIRCUIT_JOBS = "/api/v1/jobs/default/circuit"


@pytest.fixture
def iteration_server(monkeypatch: pytest.MonkeyPatch) -> Iterator[tuple[str, list[str]]]:
    """Serve the reservation and device endpoints used by the Python clients.

    Yields:
        The server URL and ordered list of requests.
    """
    requests: list[str] = []
    jobs: dict[str, tuple[str, int]] = {}
    state = "waiting"
    inventory, architecture, calibration, metrics, health = json.loads(
        Path(__file__).with_name("fixtures").joinpath("initialization.json").read_text(encoding="utf-8")
    )

    class Handler(BaseHTTPRequestHandler):
        def _reply(self, status: int, value: object) -> None:
            body = json.dumps(value).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            requests.append(f"GET {self.path}")
            if self.path == ITERATION:
                self._reply(200, {"id": ITERATION_ID, "status": state})
            elif self.path == "/api/v1/quantum-computers":
                self._reply(200, inventory)
            elif self.path.endswith("/artifacts/static-quantum-architectures"):
                self._reply(200, architecture)
            elif self.path.endswith("/dynamic-quantum-architecture"):
                self._reply(200, calibration)
            elif self.path.endswith("/metrics"):
                self._reply(200, metrics)
            elif self.path.endswith("/health"):
                self._reply(200, health)
            elif self.path in jobs:
                self._reply(200, {"status": "ready"})
            elif self.path.rsplit("/artifacts/", 1)[0] in jobs:
                job_path, artifact = self.path.rsplit("/artifacts/", 1)
                key, shots = jobs[job_path]
                if artifact == "measurement_counts":
                    self._reply(200, [{"measurement_keys": [key], "counts": {"0": shots}}])
                else:
                    self._reply(200, [{key: [[0]] * shots}])
            else:
                self.send_error(404)

        def do_POST(self) -> None:
            nonlocal state
            requests.append(f"POST {self.path}")
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            if self.path == ITERATIONS:
                request = json.loads(body)
                if request["max_duration_seconds"] != 60 or request["idle_timeout_seconds"] != 30:
                    self.send_error(400)
                    return
                self._reply(200, {"id": ITERATION_ID, "status": state})
            elif self.path == f"{ITERATION}/end":
                self._reply(202, {"id": ITERATION_ID, "status": state})
                state = "completed"
            elif self.path.split("?", 1)[0] == CIRCUIT_JOBS:
                payload = json.loads(body)
                if payload.get("dd_mode") != "disabled":
                    self.send_error(400)
                    return
                job_id = f"job-{len(jobs) + 1}"
                key = payload["circuits"][0]["instructions"][-1]["args"]["key"]
                jobs[f"/api/v1/jobs/{job_id}"] = (key, payload["shots"])
                self._reply(200, {"id": job_id})
            else:
                self.send_error(404)

        def log_message(self, format: str, *args: object) -> None:  # ruff:ignore[builtin-argument-shadowing] HTTP handler signature.
            """Suppress loopback test logs."""

    monkeypatch.setenv("no_proxy", "127.0.0.1,localhost")
    monkeypatch.delenv("IQM_TOKEN", raising=False)
    monkeypatch.delenv("IQM_TOKENS_FILE", raising=False)
    with HTTPServer(("127.0.0.1", 0), Handler) as server:
        worker = Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        worker.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}", requests
        finally:
            server.shutdown()
            worker.join()


def test_reserved_runs_and_primitives(iteration_server: tuple[str, list[str]]) -> None:
    """Exercise creation, per-job attachment and graceful end through the installed device."""
    base_url, requests = iteration_server
    backend = IQMBackend(base_url=base_url, qc_alias="default")
    backend.set_options(run_request_options={"dd_mode": "disabled"})
    circuit = QuantumCircuit(1)
    circuit.measure_all()
    requests.clear()
    with backend.reservation(max_duration_seconds=60, idle_timeout_seconds=30) as reserved:
        assert requests == [f"POST {ITERATIONS}"]
        assert reserved.device is backend.device
        assert reserved.options.reservation_id == ITERATION_ID
        assert reserved.run(circuit, shots=2).result().get_counts() == {"0": 2}
        assert reserved.sampler().run([circuit], shots=3).result()[0].data["meas"].get_counts() == {"0": 3}
        assert backend.run(circuit, shots=1).result().get_counts() == {"0": 1}
        assert backend.run(circuit, shots=1, reservation_id=UUID(ITERATION_ID)).result().get_counts() == {"0": 1}

    attached = f"POST {CIRCUIT_JOBS}?iteration_id={ITERATION_ID}"
    assert [request for request in requests if request.startswith(f"POST {CIRCUIT_JOBS}")] == [
        attached,
        attached,
        f"POST {CIRCUIT_JOBS}",
        attached,
    ]
    assert requests.count(f"POST {ITERATION}/end") == 1
    assert requests.index(f"POST {ITERATION}/end") < requests.index(f"GET {ITERATION}")
    assert backend.options.reservation_id is None


def test_bare_device_reservation(caplog: pytest.LogCaptureFixture) -> None:
    """The MQT context submits just the create and end jobs, then waits for end."""
    device = Mock(spec=Device)
    create_job = Mock(spec=Job, id=ITERATION_ID)
    end_job = Mock(spec=Job)
    device.submit_job.side_effect = [create_job, end_job]
    caplog.set_level(logging.INFO, logger="iqm.qdmi.iteration")
    with reservation(device, max_duration_seconds=60) as reservation_id:
        assert ITERATION_ID in caplog.text
        assert reservation_id == ITERATION_ID
        assert not create_job.method_calls
    assert device.submit_job.call_args_list == [
        call(json.dumps({"max_duration_seconds": 60}), ProgramFormat.CUSTOM2),
        call(json.dumps({"end": ITERATION_ID}), ProgramFormat.CUSTOM2),
    ]
    create_job.wait.assert_called_once_with(300)


@pytest.mark.parametrize(
    ("completed", "error"),
    [(False, TimeoutError), (True, RuntimeError)],
)
def test_cleanup_failure_keeps_id(*, completed: bool, error: type[Exception]) -> None:
    """Expose the reservation UUID on a timeout or failed terminal state."""
    device = Mock(spec=Device)
    create_job = Mock(id=ITERATION_ID, wait=Mock(return_value=completed), check=Mock(return_value=Job.Status.FAILED))
    device.submit_job.side_effect = [create_job, Mock()]
    with pytest.raises(error, match=ITERATION_ID), reservation(device, max_duration_seconds=60):
        pass


def test_failed_cleanup_preserves_workload_error() -> None:
    """Report both failures when end submission fails during unwinding."""
    device = Mock(spec=Device)
    device.submit_job.side_effect = [Mock(id=ITERATION_ID), RuntimeError("end refused")]
    with pytest.raises(KeyboardInterrupt) as error, reservation(device, max_duration_seconds=60):
        Mock(side_effect=KeyboardInterrupt)()
    assert error.value.__notes__ == [f"Ending IQM reservation {ITERATION_ID}: end refused"]
