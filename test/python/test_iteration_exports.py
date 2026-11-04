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

"""Installed-wheel ABI and real transport redirect checks, without IQM access."""

from __future__ import annotations

import ctypes
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest

from iqm.qdmi import IQM_QDMI_INCLUDE_DIR, IQM_QDMI_LIBRARY_PATH


class IterationResult(ctypes.Structure):
    """Match the installed caller-owned C result."""

    _fields_ = [("iteration_id", ctypes.c_char * 37), ("state", ctypes.c_char * 16), ("http_status", ctypes.c_int)]


@pytest.fixture
def iteration_library() -> ctypes.CDLL:
    """Resolve and type every public iteration symbol from the installed wheel.

    Returns:
        Library with the standalone management ABI configured.
    """
    assert (IQM_QDMI_INCLUDE_DIR / "iqm_qdmi" / "iteration.h").is_file()
    lib = ctypes.CDLL(str(IQM_QDMI_LIBRARY_PATH))
    lib.IQM_QDMI_iteration_client_alloc.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    lib.IQM_QDMI_iteration_client_alloc.restype = ctypes.c_int
    lib.IQM_QDMI_iteration_client_free.argtypes = [ctypes.c_void_p]
    lib.IQM_QDMI_iteration_client_free.restype = None
    lib.IQM_QDMI_iteration_client_diagnostics.argtypes = [ctypes.c_void_p]
    lib.IQM_QDMI_iteration_client_diagnostics.restype = ctypes.c_char_p
    lib.IQM_QDMI_iteration_create.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint64,
        ctypes.POINTER(IterationResult),
    ]
    lib.IQM_QDMI_iteration_create.restype = ctypes.c_int
    for name in ("get", "end"):
        function = getattr(lib, f"IQM_QDMI_iteration_{name}")
        function.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64, ctypes.POINTER(IterationResult)]
        function.restype = ctypes.c_int
    return lib


@pytest.mark.parametrize("status", [301, 302, 307, 308])
def test_management_never_replays_redirect(iteration_library: ctypes.CDLL, status: int) -> None:
    """A real CPR request must not follow redirects or forward authorization."""
    requests: list[tuple[str, str | None]] = []

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:
            requests.append((self.path, self.headers.get("Authorization")))
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            if self.path == "/api/v1/iterations":
                self.send_response(status)
                self.send_header("Location", "/redirected")
            else:
                self.send_response(200)
            self.end_headers()

        do_GET = do_POST  # ruff: ignore[mixed-case-variable-in-class-scope] - HTTP handler protocol.

        def log_message(self, format: str, *args: object) -> None:  # ruff: ignore[builtin-argument-shadowing]
            """Suppress local HTTP test logs."""

    server = HTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    client = ctypes.c_void_p()
    try:
        url = f"http://127.0.0.1:{server.server_port}".encode()
        assert (
            iteration_library.IQM_QDMI_iteration_client_alloc(url, b"test-token", None, 1000, ctypes.byref(client)) == 0
        )
        result = IterationResult()
        assert iteration_library.IQM_QDMI_iteration_create(client, b"qc", 60, 0, 1000, ctypes.byref(result)) != 0
        assert result.http_status == status
        assert requests == [("/api/v1/iterations", "Bearer test-token")]
    finally:
        iteration_library.IQM_QDMI_iteration_client_free(client)
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)
