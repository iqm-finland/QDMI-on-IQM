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

"""Reject alternate trust when the native transport selects a PEM bundle."""

from __future__ import annotations

import contextlib
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread


class Handler(BaseHTTPRequestHandler):
    """Return an empty successful response for the TLS probe."""

    def do_GET(self) -> None:
        """Reply without external services or credentials."""
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, format: str, *args: object) -> None:  # ruff: ignore[builtin-argument-shadowing]
        """Suppress request logs."""

    def handle(self) -> None:
        """Accept clients that abort before HTTP when certificate checks fail."""
        with contextlib.suppress(ConnectionResetError, ssl.SSLError):
            super().handle()


def main() -> int:
    """Exercise the native CA configuration against a local TLS server.

    Returns:
        Zero on success, or the CTest skip code for unsupported CA directories.
    """
    fixtures = Path(__file__).resolve().parents[1] / "fixtures" / "tls"
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server.key")
    with tempfile.TemporaryDirectory() as directory, ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        capath = Path(directory)
        # OpenSSL's subject hash for the disposable CA, generated with x509 -subject_hash.
        shutil.copyfile(fixtures / "ca.pem", capath / "83d667e8.0")
        shutil.copyfile(fixtures / "unrelated.pem", capath / "control.pem")
        malformed = capath / "malformed.pem"
        malformed.write_text("not a certificate", encoding="utf-8")
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"https://127.0.0.1:{server.server_port}"
            cases = [
                ("-", True),
                ((fixtures / "ca.pem").as_posix(), True),
                ((fixtures / "unrelated.pem").as_posix(), False),
                (malformed.as_posix(), False),
                ((capath / "missing.pem").as_posix(), False),
            ]
            for bundle, expected in cases:
                result = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true] -- CTest supplies the local probe
                    [sys.argv[1], url, bundle, capath.as_posix()], check=False, timeout=10
                )
                if result.returncode == 125:
                    # Schannel also requires revocation data absent from this fixture.
                    return 77
                assert (result.returncode == 0) == expected, (bundle, expected, result.returncode)
            for bundle, expected in cases[1:]:
                environment = {**os.environ, "CURL_CA_BUNDLE": bundle, "NO_PROXY": "*"}
                result = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true] -- CTest supplies the local probe
                    [sys.argv[1], url], env=environment, check=False, timeout=15
                )
                assert (result.returncode == 0) == expected, ("GET", bundle, expected, result.returncode)
        finally:
            server.shutdown()
            thread.join(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
