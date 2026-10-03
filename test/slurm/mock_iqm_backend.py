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

# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

"""Minimal local IQM API used by the Docker SPANK tests."""

from __future__ import annotations

import argparse
import contextlib
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from typing import ClassVar


class IQMRequestHandler(BaseHTTPRequestHandler):
    """Serve the IQM endpoints required by QDMI session initialization."""

    request_count: ClassVar[int] = 0
    jobs: ClassVar[dict[str, tuple[int, list[str]]]] = {}
    lock: ClassVar[Lock] = Lock()

    def do_GET(self) -> None:
        """Return a minimal successful response for a supported IQM endpoint."""
        path = self.path
        if path == "/request-count":
            self._send_json(self.request_count)
            return
        if not self.headers.get("Authorization", "").startswith("Bearer fixture."):
            self.send_error(401)
            return

        type(self).request_count += 1
        if path.startswith("/hang/"):
            time.sleep(3)
            path = path.removeprefix("/hang")

        if path == "/api/v1/quantum-computers":
            self._send_json({
                "quantum_computers": [
                    {"id": "qc-default", "alias": "default"},
                    {"id": "qc-emerald", "alias": "emerald"},
                    {"id": "qc-emerald-mock", "alias": "emerald:mock"},
                ]
            })
            return
        if path.endswith("/artifacts/static-quantum-architectures"):
            self._send_json([
                {
                    "qubits": ["QB1", "QB2"],
                    "computational_resonators": [],
                    "connectivity": [["QB1", "QB2"]],
                }
            ])
            return
        if path.endswith("/dynamic-quantum-architecture"):
            self._send_json({
                "calibration_set_id": "00000000-0000-0000-0000-000000000000",
                "qubits": ["QB1", "QB2"],
                "computational_resonators": [],
                "gates": {
                    name: {
                        "implementations": {"fixture": {"loci": loci}},
                        "default_implementation": "fixture",
                        "override_default_implementation": {},
                    }
                    for name, loci in {
                        "prx": [["QB1"], ["QB2"]],
                        "cz": [["QB1", "QB2"]],
                        "measure": [["QB1"], ["QB2"]],
                    }.items()
                },
            })
            return
        if path.endswith("/metrics"):
            self._send_json({"observations": []})
            return
        if path == "/cocos/health":
            self._send_json({"services": [], "warnings": []})
            return
        if path.endswith("/health"):
            self._send_json({"healthy": True})
            return
        if path.endswith("/queue-availability"):
            self._send_json({"queue_length": 0, "available": ["normal"]})
            return
        if path.startswith("/api/v1/jobs/"):
            job_id = path.split("/")[4]
            with self.lock:
                shots, keys = self.jobs[job_id]
            if path.endswith("/measurement_counts"):
                self._send_json([
                    {
                        "measurement_keys": keys,
                        "counts": {"00": shots // 2, "11": shots // 2},
                    }
                ])
            elif path.endswith("/measurements"):
                self._send_json([{key: [[index % 2] for index in range(shots)] for key in keys}])
            else:
                self._send_json({"status": "ready"})
            return

        self.send_error(404)

    def do_POST(self) -> None:
        """Accept an IQM JSON circuit and retain its measurement-key order."""
        if not self.headers.get("Authorization", "").startswith("Bearer fixture."):
            self.send_error(401)
            return
        if self.path != "/api/v1/jobs/emerald/circuit":
            self.send_error(404)
            return
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        circuit = payload["circuits"][0]
        keys = [
            instruction["args"]["key"] for instruction in circuit["instructions"] if instruction["name"] == "measure"
        ]
        assert len(keys) == 2
        assert any(instruction["name"] == "cz" for instruction in circuit["instructions"])
        with self.lock:
            job_id = str(len(self.jobs) + 1)
            self.jobs[job_id] = (payload["shots"], keys)
        self._send_json({"id": job_id})

    def log_message(self, format: str, *args: object) -> None:  # ruff:ignore[builtin-argument-shadowing]
        """Suppress routine request logs in the test container."""

    def _send_json(self, payload: object) -> None:
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        with contextlib.suppress(BrokenPipeError):
            self.wfile.write(body)


def main() -> None:
    """Run the mock server until the test container exits."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=18080, type=int)
    args = parser.parse_args()
    ThreadingHTTPServer((args.host, args.port), IQMRequestHandler).serve_forever()


if __name__ == "__main__":
    main()
