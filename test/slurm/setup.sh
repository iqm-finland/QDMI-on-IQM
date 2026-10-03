#!/bin/sh
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

set -eu

uv run --no-project python - <<'CATALOGUE'
import base64
import json
import os
from pathlib import Path
from iqm.qdmi import IQM_QDMI_LIBRARY_PATH

library = IQM_QDMI_LIBRARY_PATH
if os.environ["PROVIDER_INSTALL_MODE"] == "native":
    library = Path("/opt/provider-native/lib/libiqm-qdmi-device.so")
configuration = {
    "schema-version": 1,
    "qdmi": {"devices": [{
        "id": "iqm.fixture.emerald",
        "library": str(library.resolve()),
        "prefix": "IQM",
        "enabled": True,
        "session": {"base-url": "http://iqm:18080", "custom1": "qc-emerald"},
    }]},
}
Path("/opt/provider-catalogue.json").write_text(json.dumps(configuration))
# Deliberately unsigned fixture credential; never accepted by a real IQM server.
payload = base64.urlsafe_b64encode(json.dumps({"exp": 9223372036854775807}).encode()).decode().rstrip("=")
Path("/opt/iqm-fixture-tokens.json").write_text(json.dumps({"access_token": f"fixture.{payload}.signature"}))
CATALOGUE
