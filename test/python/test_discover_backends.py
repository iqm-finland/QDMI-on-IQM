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

"""Offline regression tests for the discovery example."""

from __future__ import annotations

import importlib.util
import json
import logging
import sys
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import Mock

import pytest
import requests
from mqt.core.qdmi import Device

EXAMPLE_VALUE = "example-token"

if TYPE_CHECKING:
    from types import ModuleType


@pytest.fixture
def discovery(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    """Load the example with isolated credentials and no server access.

    Returns:
        The discovery example module.
    """
    for name in ("IQM_TOKEN", "IQM_TOKENS_FILE", "IQM_SERVER_URL", "IQM_BASE_URL"):
        monkeypatch.delenv(name, raising=False)
    path = Path(__file__).resolve().parents[2] / "examples" / "discover_backends.py"
    spec = importlib.util.spec_from_file_location("discover_backends", path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module.requests, "get", Mock(side_effect=AssertionError("Unexpected server access")))
    monkeypatch.setattr(module, "register_device_if_absent", Mock())
    return module


def test_base_url_precedence(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """The discovery URL follows the IQM backend's environment precedence."""
    assert discovery.resolve_base_url(None) == "https://resonance.iqm.tech"
    monkeypatch.setenv("IQM_BASE_URL", "https://alias.example")
    assert discovery.resolve_base_url(None) == "https://alias.example"
    monkeypatch.setenv("IQM_SERVER_URL", "https://server.example")
    assert discovery.resolve_base_url(None) == "https://server.example"
    assert discovery.resolve_base_url("https://explicit.example") == "https://explicit.example"


def test_credentials(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Resolve one authentication source and reject conflicting sources."""
    assert discovery.resolve_credentials(None, None) == (None, None)
    monkeypatch.setenv("IQM_TOKEN", "environment-token")
    assert discovery.resolve_credentials(None, None) == ("environment-token", None)
    assert discovery.resolve_credentials("explicit-token", None) == ("explicit-token", None)
    monkeypatch.setenv("IQM_TOKENS_FILE", "tokens.json")
    assert discovery.resolve_credentials("explicit-token", None) == ("explicit-token", None)
    assert discovery.resolve_credentials(None, "explicit.json") == (None, "explicit.json")
    with pytest.raises(ValueError, match="Provide only one"):
        discovery.resolve_credentials("explicit-token", "explicit.json")
    with pytest.raises(ValueError, match="Provide only one"):
        discovery.resolve_credentials(None, None)
    monkeypatch.delenv("IQM_TOKEN")
    assert discovery.resolve_credentials(None, None) == (None, "tokens.json")


@pytest.mark.parametrize(
    "payload", [{}, {"quantum_computers": {}}, {"quantum_computers": [None]}, {"quantum_computers": [{"id": ""}]}]
)
def test_inventory_validation(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, payload: object) -> None:
    """Reject malformed inventories instead of selecting an unintended computer."""
    response = Mock()
    response.json.return_value = payload
    monkeypatch.setattr(discovery.requests, "get", Mock(return_value=response))
    with pytest.raises((TypeError, ValueError), match="Expected"):
        discovery.list_quantum_computers("https://server.example", None)


def test_inventory_request(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Send the inventory request with credentials, timeout, and HTTP validation."""
    response = Mock()
    response.json.return_value = {"quantum_computers": [{"id": "qc-1"}]}
    get = Mock(return_value=response)
    monkeypatch.setattr(discovery.requests, "get", get)
    assert discovery.list_quantum_computers("https://server.example/", EXAMPLE_VALUE) == [{"id": "qc-1"}]
    get.assert_called_once_with(
        "https://server.example/api/v1/quantum-computers", headers={"Authorization": "Bearer example-token"}, timeout=30
    )
    response.raise_for_status.assert_called_once_with()
    response.raise_for_status.side_effect = requests.HTTPError("Unauthorized")
    with pytest.raises(requests.HTTPError):
        discovery.list_quantum_computers("https://server.example", None)


@pytest.mark.parametrize("min_qubits", [5, 11])
def test_selection_and_failed_properties(
    discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture, min_qubits: int
) -> None:
    """Use explicit IDs, skip failures, and select by size with stable ties."""
    inventory = [{"id": name} for name in ("broken", "small", "large", "tied")]
    monkeypatch.setattr(discovery, "list_quantum_computers", Mock(return_value=inventory))
    monkeypatch.setenv("IQM_QC_ID", "unrelated-computer")
    broken = Mock(spec=Device)
    broken.qubits_num.side_effect = RuntimeError("Property unavailable")
    devices = [broken]
    for size in (6, 10, 10):
        device = Mock(spec=Device)
        device.qubits_num.return_value = size
        device.status.return_value = Device.Status.BUSY
        device.operations.return_value = []
        device.regular_sites.return_value = []
        devices.append(device)
    opened = Mock(side_effect=devices)
    monkeypatch.setattr(discovery, "open_device", opened)
    caplog.set_level(logging.INFO)
    if min_qubits <= 10:
        discovery.run_discovery(
            base_url="https://server.example", token=EXAMPLE_VALUE, tokens_file=None, min_qubits=min_qubits
        )
        assert "Selected 'large': 10 qubits" in caplog.text
        assert "Selected 'tied'" not in caplog.text
    else:
        with pytest.raises(SystemExit, match="largest available: 10"):
            discovery.run_discovery(
                base_url="https://server.example", token=EXAMPLE_VALUE, tokens_file=None, min_qubits=min_qubits
            )
    assert "Skipping 'broken'" in caplog.text
    assert [call.kwargs["custom1"] for call in opened.call_args_list] == [qc["id"] for qc in inventory]
    assert all(
        call.kwargs["token"] == EXAMPLE_VALUE and call.kwargs["auth_file"] is None for call in opened.call_args_list
    )


def test_tokens_file(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Read the listing token and pass the same file to the native device session."""
    tokens_file = tmp_path / "tokens.json"
    tokens_file.write_text(json.dumps({"access_token": EXAMPLE_VALUE}), encoding="utf-8")
    listing = Mock(return_value=[{"id": "qc-1"}])
    monkeypatch.setattr(discovery, "list_quantum_computers", listing)
    opened = Mock(side_effect=RuntimeError("Cannot open device"))
    monkeypatch.setattr(discovery, "open_device", opened)
    with pytest.raises(SystemExit, match="largest available: 0"):
        discovery.run_discovery(
            base_url="https://server.example", token=None, tokens_file=str(tokens_file), min_qubits=5
        )
    listing.assert_called_once_with("https://server.example", EXAMPLE_VALUE)
    assert opened.call_args.kwargs["auth_file"] == str(tokens_file)
    assert opened.call_args.kwargs["token"] is None
    tokens_file.write_text(json.dumps({"access_token": 123}), encoding="utf-8")
    with pytest.raises(ValueError, match="nonempty string access_token"):
        discovery.run_discovery(base_url=None, token=None, tokens_file=str(tokens_file), min_qubits=5)
    assert listing.call_count == 1


def test_empty_inventory(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Report an empty server inventory before opening devices."""
    monkeypatch.setattr(discovery, "list_quantum_computers", Mock(return_value=[]))
    opened = Mock()
    monkeypatch.setattr(discovery, "open_device", opened)
    with pytest.raises(SystemExit, match="No quantum computers reported"):
        discovery.run_discovery(base_url=None, token=None, tokens_file=None, min_qubits=5)
    opened.assert_not_called()


def test_calibration_summaries(discovery: ModuleType) -> None:
    """Average only available calibration metrics and preserve missing data."""
    device = Mock(spec=Device)
    operation = Mock()
    operation.name.return_value = "cz"
    operation.site_pairs.return_value = [(1, 2), (2, 3), (3, 4)]
    operation.fidelity.side_effect = [0.9, None, 1.0]
    device.operations.return_value = [operation]
    assert discovery.calibration_summary(device) == "'cz' mean 2-qubit fidelity 0.9500 over 2 pair(s)"
    device.operations.return_value = []
    assert discovery.calibration_summary(device) == "no two-qubit gate fidelity exposed by the device"
    sites = [Mock(), Mock()]
    sites[0].t1.return_value, sites[0].t2.return_value = 100, None
    sites[1].t1.return_value, sites[1].t2.return_value = None, 80
    device.regular_sites.return_value = sites
    device.duration_unit.return_value = "us"
    assert discovery.coherence_summary(device) == "mean T1 100.0 us, mean T2 80.0 us"
    device.regular_sites.return_value = []
    assert discovery.coherence_summary(device) == "no T1/T2 data exposed by the device"


def test_simulator(discovery: ModuleType, caplog: pytest.LogCaptureFixture) -> None:
    """Open real DDSIM directly without contacting the inventory endpoint."""
    caplog.set_level(logging.INFO)
    discovery.run_simulator(5)
    assert "Selected backend: 'MQT Core DDSIM QDMI Device'" in caplog.text
    with pytest.raises(SystemExit, match="fewer than"):
        discovery.run_simulator(sys.maxsize)


@pytest.mark.parametrize("min_qubits", ["0", "-1"])
def test_positive_minimum(
    discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], min_qubits: str
) -> None:
    """Reject nonpositive constraints before opening a backend."""
    monkeypatch.setattr(sys, "argv", ["discover_backends.py", "--min-qubits", min_qubits])
    with pytest.raises(SystemExit) as exc_info:
        discovery.main()
    assert exc_info.value.code == 2
    assert "--min-qubits must be positive" in capsys.readouterr().err


def test_http_error_redacts_credentials(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Do not expose exception text containing authentication data."""
    monkeypatch.setattr(sys, "argv", ["discover_backends.py"])
    monkeypatch.setattr(discovery.requests, "get", Mock(side_effect=requests.HTTPError("secret-token")))
    with pytest.raises(SystemExit, match="Check the server URL") as exc_info:
        discovery.main()
    assert "secret-token" not in str(exc_info.value)
