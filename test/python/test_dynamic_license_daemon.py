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

"""Offline checks for the dynamic-license daemon's QDMI adapter."""

from __future__ import annotations

import importlib
from pathlib import Path
from unittest.mock import Mock

import pytest


def test_load_device_uses_installed_manifest(monkeypatch: pytest.MonkeyPatch) -> None:
    """Open an independent session with explicit daemon configuration."""
    from mqt.core.qdmi import builtin_driver  # ruff: ignore[import-outside-top-level]

    from iqm.qdmi import IQM_QDMI_DEVICE_ID  # ruff: ignore[import-outside-top-level]

    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2] / "spank"))
    daemon = importlib.import_module("dynamic_license_daemon")
    config = daemon.parse_args([
        "--resource-name",
        "qc",
        "--base-url",
        "https://server.example",
        "--qc-id",
        "qc-1",
        "--token",
        "example-token",
    ])
    opened = Mock()
    monkeypatch.setattr(builtin_driver, "open_device", opened)
    assert daemon.load_device(config) is opened.return_value
    opened.assert_called_once_with(
        IQM_QDMI_DEVICE_ID,
        base_url=config.base_url,
        token=config.token,
        auth_file=config.tokens_file,
        custom1=config.qc_id,
        custom2=config.qc_alias,
    )


@pytest.mark.parametrize("available", [False, True])
def test_run_retries_failed_license_update(monkeypatch: pytest.MonkeyPatch, *, available: bool) -> None:
    """Retry a failed update, then skip unchanged successful updates."""
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2] / "spank"))
    daemon = importlib.import_module("dynamic_license_daemon")
    config = daemon.parse_args(["--qc-alias", "qc"])
    signal = Mock()
    signal.probe.return_value = available
    monkeypatch.setattr(daemon, "build_signals", Mock(return_value=[signal]))
    update = Mock(side_effect=[Mock(returncode=1, stderr="failed"), Mock(returncode=0, stdout="")])
    monkeypatch.setattr(daemon.subprocess, "run", update)
    monkeypatch.setattr(daemon.time, "sleep", Mock(side_effect=[None, None, KeyboardInterrupt]))

    with pytest.raises(KeyboardInterrupt):
        daemon.run(config)

    assert signal.probe.call_count == 3
    assert update.call_count == 2
    assert update.call_args.args[0][-1] == f"lastconsumed={0 if available else 1}"
