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

"""Exercise compressed responses through the installed native HTTP transport."""

from __future__ import annotations

import gzip
import json
import threading
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest
from mqt.core.qdmi.builtin_driver import open_device

from iqm.qdmi import IQM_QDMI_DEVICE_ID


@pytest.mark.parametrize("encoding", ["gzip", "deflate", "br", "zstd"])
def test_compressed_responses(encoding: str) -> None:
    """Negotiate compression and decode metadata before opening a device."""
    # Precompressed quantum-computers response below avoids extra Python dependencies.
    compressed_computers = {
        "br": bytes.fromhex(
            "1b490000c46fe74fabdeccb0985649e2d415fceea85f021b70e09e27e30483f01d5416c88b83bfa9acf19d4da2f026"
            "5c70853768fcef23044b73e7ec413c42aabca66bf2e203"
        ),
        "zstd": bytes.fromhex(
            "28b52ffd204a350200a4037b227175616e74756d5f636f6d707574657273223a205b7b226964223a2022666978747572"
            "65222c2022616c69617372657373696f6e2d7d5d7d030043113315e117cc32"
        ),
    }
    responses = {
        "/api/v1/quantum-computers": {"quantum_computers": [{"id": "fixture", "alias": "compression-fixture"}]},
        "/api/v1/quantum-computers/compression-fixture/artifacts/static-quantum-architectures": [
            {
                "qubits": ["QB1", "QB2"],
                "connectivity": [["QB1", "QB2"]],
                "description": "compressed metadata " * 65536,
            }
        ],
        "/api/v1/calibration-sets/compression-fixture/default/dynamic-quantum-architecture": {
            "calibration_set_id": "fixture",
            "gates": {},
        },
        "/api/v1/calibration-sets/compression-fixture/fixture/metrics": {"observations": []},
        "/cocos/health": {},
    }
    accepted: dict[str, set[str]] = {}
    compress = gzip.compress if encoding == "gzip" else zlib.compress

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:  # ruff: ignore[builtin-argument-shadowing]
            """Silence the local fixture server."""

        def do_GET(self) -> None:
            """Serve compressed metadata using the cloud API response shapes."""
            accepted[self.path] = set(self.headers.get("Accept-Encoding", "").replace(" ", "").split(","))
            data = json.dumps(responses.get(self.path, {})).encode()
            compressed = encoding not in compressed_computers or self.path == "/api/v1/quantum-computers"
            if compressed:
                data = compressed_computers[encoding] if encoding in compressed_computers else compress(data)
            self.send_response(200 if self.path in responses else 404)
            self.send_header("Content-Type", "application/json")
            if compressed:
                self.send_header("Content-Encoding", encoding)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            device = open_device(
                IQM_QDMI_DEVICE_ID,
                base_url=f"http://127.0.0.1:{server.server_port}",
                token="fixture",  # ruff: ignore[hardcoded-password-func-arg] - synthetic local credential
                custom2="compression-fixture",
            )
            assert device.name() == "compression-fixture"
            assert device.qubits_num() == 2
            del device
        finally:
            server.shutdown()
            thread.join()

    assert accepted.keys() == responses.keys()
    assert all({"gzip", "deflate", "br", "zstd"} <= encodings for encodings in accepted.values())
