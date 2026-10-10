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

python3 - "${1:-/opt/provider-catalogue.json}" <<'CATALOGUE'
import json
import os
import sys
from pathlib import Path
from iqm.qdmi import IQM_QDMI_LIBRARY_PATH

catalogue = IQM_QDMI_LIBRARY_PATH.parent / "iqm-qdmi-device.qdmi.json"
presets = json.loads(catalogue.read_text())["qdmi"]["devices"]
library = IQM_QDMI_LIBRARY_PATH
if os.environ["PROVIDER_INSTALL_MODE"] == "native":
    library = Path("/opt/provider-native/lib/libiqm-qdmi-device.so")
for preset in presets:
    preset["library"] = str(library.resolve())
    preset["enabled"] = preset["id"] == "iqm.emerald.mock"
configuration = {"schema-version": 1, "qdmi": {"devices": presets}}
Path(sys.argv[1]).write_text(json.dumps(configuration))
CATALOGUE
