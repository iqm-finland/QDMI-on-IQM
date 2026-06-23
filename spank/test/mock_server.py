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

# ruff: noqa: INP001
"""Mock server for Slurm SPANK integration tests."""

import contextlib
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class MockHandler(BaseHTTPRequestHandler):
    """Simple HTTP request handler for the mock server."""

    def do_GET(self) -> None:
        """Handle HTTP GET requests."""
        if self.path == "/api/v1/quantum-computers":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            response = {
                "quantum_computers": [
                    {"alias": "emerald", "id": "emerald-id", "status": "online"},
                    {"alias": "garnet", "id": "garnet-id", "status": "offline"},
                ]
            }
            self.wfile.write(json.dumps(response).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()


def run(port: int = 8080) -> None:
    """Run the mock HTTP server."""
    server_address = ("", port)
    httpd = HTTPServer(server_address, MockHandler)
    sys.stdout.flush()
    with contextlib.suppress(KeyboardInterrupt):
        httpd.serve_forever()
    httpd.server_close()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    run(port)
