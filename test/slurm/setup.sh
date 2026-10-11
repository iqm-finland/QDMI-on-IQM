#!/bin/sh
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
