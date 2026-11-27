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
import pickle  # ruff:ignore[suspicious-pickle-import] Round-trip a locally constructed exception.
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from threading import Thread
from typing import TYPE_CHECKING
from unittest.mock import Mock
from uuid import UUID

import pytest
from qiskit import QuantumCircuit

from iqm.qdmi import iteration
from iqm.qdmi.iteration import IterationClient, IterationError, IterationResult
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
                qc = json.loads(body)["qc"]
                if qc == "uncertain":
                    self._reply(500, {"request_id": "uncertain-request"})
                else:
                    result_state = "unrecognized" if qc == "partial" else state
                    self._reply(200, {"id": ITERATION_ID, "status": result_state})
                    state = "active"
            elif self.path == f"{ITERATION}/end":
                self._reply(202, {"id": ITERATION_ID, "status": state})
                state = "completed"
            elif self.path.split("?", 1)[0] == CIRCUIT_JOBS:
                payload = json.loads(body)
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


def test_manage_iteration_and_attach_independent_backends(iteration_server: tuple[str, list[str]]) -> None:
    """Send both direct and primitive jobs into one reservation created before any session."""
    base_url, requests = iteration_server
    with IterationClient(base_url=base_url) as client:
        assert client.create("default", 60) == IterationResult(ITERATION_ID, "waiting", 200)
        assert client.get(UUID(ITERATION_ID)).state == "active"
        first = IQMBackend(base_url=base_url, qc_alias="default", iteration_id=ITERATION_ID.upper())
        second = IQMBackend(base_url=base_url, qc_alias="default", iteration_id=UUID(ITERATION_ID))
        ordinary = IQMBackend(base_url=base_url, qc_alias="default")
        circuit = QuantumCircuit(1)
        circuit.measure_all()
        assert first.run(circuit, shots=2).result().get_counts() == {"0": 2}
        assert second.sampler().run([circuit], shots=3).result()[0].data["meas"].get_counts() == {"0": 3}
        assert ordinary.run(circuit, shots=1).result().get_counts() == {"0": 1}
        assert client.end(ITERATION_ID) == IterationResult(ITERATION_ID, "active", 202)
        assert client.get(ITERATION_ID).state == "completed"

    attached = f"POST {CIRCUIT_JOBS}?iteration_id={ITERATION_ID}"
    assert [request for request in requests if request.startswith(f"POST {CIRCUIT_JOBS}")] == [
        attached,
        attached,
        f"POST {CIRCUIT_JOBS}",
    ]
    assert requests[:2] == [f"POST {ITERATIONS}", f"GET {ITERATION}"]
    assert requests[-2:] == [f"POST {ITERATION}/end", f"GET {ITERATION}"]


def test_failed_creation_keeps_recovery_information(iteration_server: tuple[str, list[str]]) -> None:
    """Keep a confirmed ID and copy error diagnostics before another request."""
    base_url, requests = iteration_server
    with IterationClient(base_url=base_url) as client:
        with pytest.raises(IterationError) as partial:
            client.create("partial", 60)
        with pytest.raises(IterationError) as uncertain:
            client.create("uncertain", 60)
        assert partial.value.qdmi_status != 0
        assert partial.value.result == IterationResult(ITERATION_ID, None, 200)
        assert partial.value.diagnostics["message"] == "Unknown iteration state: unrecognized"
        assert uncertain.value.result.iteration_id is None
        assert uncertain.value.diagnostics["creation_uncertain"] is True
        restored = pickle.loads(pickle.dumps(partial.value))  # ruff: ignore[suspicious-pickle-usage] -- Round-trip our own exception.
        assert restored.result == partial.value.result
        assert restored.diagnostics == partial.value.diagnostics

    client.close()
    with pytest.raises(RuntimeError, match="closed"):
        client.get(ITERATION_ID)
    assert requests == [f"POST {ITERATIONS}", f"POST {ITERATIONS}"]


def test_interrupted_creation_exposes_confirmed_id(
    iteration_server: tuple[str, list[str]], monkeypatch: pytest.MonkeyPatch
) -> None:
    """Keep the ID when Python interrupts a completed native call."""
    base_url, requests = iteration_server
    lib = iteration._load_library()  # ruff:ignore[private-member-access] Patch the native call boundary.
    create = lib.IQM_QDMI_iteration_create

    def interrupted(*args: object) -> int:
        assert create(*args) == 0
        raise KeyboardInterrupt

    monkeypatch.setattr(lib, "IQM_QDMI_iteration_create", interrupted)
    with IterationClient(base_url=base_url) as client:
        with pytest.raises(KeyboardInterrupt) as error:
            client.create("default", 60)
        assert error.value.__dict__["result"].iteration_id == ITERATION_ID
        assert ITERATION_ID in str(error.value.__notes__)
        client.end(ITERATION_ID)

    assert requests == [f"POST {ITERATIONS}", f"POST {ITERATION}/end"]


def test_iteration_rejects_invalid_values_before_transport(iteration_server: tuple[str, list[str]]) -> None:
    """Prevent integer wrapping, invalid paths and C string truncation."""
    base_url, requests = iteration_server
    with IterationClient(base_url=base_url) as client:
        with pytest.raises(ValueError, match="max_duration_seconds"):
            client.create("default", 2**32 + 1)
        with pytest.raises(ValueError, match="idle_timeout_seconds"):
            client.create("default", 60, idle_timeout_seconds=-1)
        with pytest.raises(ValueError, match="timeout_ms"):
            client.get(ITERATION_ID, timeout_ms=2**64 + 1)
        with pytest.raises(ValueError, match="UUID"):
            client.end("../other")
        with pytest.raises(ValueError, match="NUL"):
            client.create("default\0other", 60)
        with pytest.raises(ValueError, match="already-open device"):
            IQMBackend(device=Mock(), iteration_id=ITERATION_ID)

    assert requests == []
