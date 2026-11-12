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

"""Native IQM sessions backed by a local HTTP fixture."""

from __future__ import annotations

import json
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from threading import Thread
from typing import TYPE_CHECKING

import pytest

from iqm.qdmi.qiskit import IQMBackend

if TYPE_CHECKING:
    from collections.abc import Iterator


@pytest.fixture
def offline_submissions() -> list[dict]:
    """Capture payloads received by the local IQM server.

    Returns:
        The submission list populated by the HTTP handler.
    """
    return []


@pytest.fixture
def offline_backend(
    monkeypatch: pytest.MonkeyPatch, request: pytest.FixtureRequest, offline_submissions: list[dict]
) -> Iterator[IQMBackend]:
    """Open the installed native library without contacting an IQM service.

    Yields:
        A backend backed by the local HTTP server.
    """
    metadata = json.loads((Path(__file__).parent / "fixtures/initialization.json").read_text())
    variant = getattr(request, "param", None)
    if variant in {"missing_prx", "missing_measure"}:
        gates = ("prx", "prx_12") if variant == "missing_prx" else ("measure",)
        for gate in gates:
            for implementation in metadata[2]["gates"][gate]["implementations"].values():
                implementation["loci"] = [locus for locus in implementation["loci"] if "QB1" not in locus]
    if variant == "extra_operations":
        metadata[2]["gates"]["measure_012"] = metadata[2]["gates"]["measure"]
    responses = deque(metadata)
    if variant == "pulse":
        responses.extend([{"status": "ready", "type": "run"}, b"\x08\x00\xff\x00"] * 2)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if responses:
                response = responses.popleft()
            else:
                self.send_error(500, "Unexpected request")
                return
            body = response if isinstance(response, bytes) else json.dumps(response).encode()
            self.send_response(200)
            self.send_header(
                "Content-Type", "application/protobuf" if isinstance(response, bytes) else "application/json"
            )
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self) -> None:
            payload = self.rfile.read(int(self.headers["Content-Length"]))
            offline_submissions.append(
                {"pulse": payload, "expect": self.headers.get("Expect")}
                if self.headers["Content-Type"] == "application/protobuf"
                else json.loads(payload)
            )
            body = json.dumps({"id": "offline-job"}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: object) -> None:  # ruff:ignore[builtin-argument-shadowing] HTTP handler signature.
            pass

    monkeypatch.setenv("no_proxy", "127.0.0.1,localhost")
    with HTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield IQMBackend(
                base_url=f"http://127.0.0.1:{server.server_port}",
                token="offline",  # ruff:ignore[hardcoded-password-func-arg] Local test server only.
                qc_alias="default",
            )
        finally:
            server.shutdown()
            thread.join()
