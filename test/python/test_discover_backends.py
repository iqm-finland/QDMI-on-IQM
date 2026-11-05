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

"""Regression tests for the discovery example."""

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
    "payload",
    [
        {"quantum_computers": [None]},
        {"quantum_computers": [{"id": ""}]},
        {"quantum_computers": [{"id": "qc-1"}]},
        {"quantum_computers": [{"id": "qc-1", "alias": ""}]},
        {"quantum_computers": [{"id": "qc-1", "alias": 1}]},
    ],
)
def test_inventory_validation(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, payload: object) -> None:
    """Reject malformed inventories instead of selecting an unintended computer."""
    response = Mock()
    response.json.return_value = payload
    monkeypatch.setattr(discovery.requests, "get", Mock(return_value=response))
    with pytest.raises(ValueError, match="Expected"):
        discovery.list_quantum_computers("https://server.example", None)


@pytest.mark.parametrize("payload", [{}, {"quantum_computers": {}}])
def test_invalid_inventory_shape(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, payload: object) -> None:
    """Report unexpected inventory types without hiding unrelated TypeErrors."""
    response = Mock()
    response.json.return_value = payload
    monkeypatch.setattr(discovery.requests, "get", Mock(return_value=response))
    with pytest.raises(discovery.InvalidServerResponseError, match="Expected"):
        discovery.list_quantum_computers("https://server.example", None)


def test_inventory_request(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Send the inventory request with credentials, timeout, and HTTP validation."""
    response = Mock()
    response.json.return_value = {"quantum_computers": [{"id": "qc-1", "alias": "QC 1"}]}
    get = Mock(return_value=response)
    monkeypatch.setattr(discovery.requests, "get", get)
    assert discovery.list_quantum_computers("https://server.example/", EXAMPLE_VALUE) == [
        {"id": "qc-1", "alias": "QC 1"}
    ]
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
    inventory = [{"id": name, "alias": name} for name in ("broken", "small", "large", "tied")]
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
    listing = Mock(return_value=[{"id": "qc-1", "alias": "QC 1"}])
    monkeypatch.setattr(discovery, "list_quantum_computers", listing)
    opened = Mock(side_effect=RuntimeError("Cannot open device"))
    monkeypatch.setattr(discovery, "open_device", opened)
    with pytest.raises(SystemExit, match="Could not query properties for any of 1 quantum computers"):
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
    monkeypatch.setenv("IQM_TOKEN", EXAMPLE_VALUE)
    monkeypatch.setattr(sys, "argv", ["discover_backends.py"])
    monkeypatch.setattr(discovery.requests, "get", Mock(side_effect=requests.HTTPError(EXAMPLE_VALUE)))
    with pytest.raises(SystemExit, match="Check the server URL") as exc_info:
        discovery.main()
    assert EXAMPLE_VALUE not in str(exc_info.value)


@pytest.mark.parametrize(
    "base_url",
    [
        "https://alice:password@example.com",
        "http://alice:password@example.com",
        "https://@example.com",
        "https://:password@example.com",
    ],
)
def test_url_credentials_are_rejected_before_transport(
    discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture, base_url: str
) -> None:
    """Never forward credentials embedded in the server URL."""
    get = Mock()
    open_device = Mock()
    monkeypatch.setattr(discovery.requests, "get", get)
    monkeypatch.setattr(discovery, "open_device", open_device)
    caplog.set_level(logging.INFO)
    with pytest.raises(ValueError, match="Credentials in server URLs are not supported"):
        discovery.list_quantum_computers(base_url, None)
    get.assert_not_called()
    with pytest.raises(ValueError, match="Credentials in server URLs are not supported"):
        discovery.run_discovery(base_url=base_url, token=None, tokens_file=None, min_qubits=5)
    get.assert_not_called()
    open_device.assert_not_called()
    assert "alice" not in caplog.text
    assert "password" not in caplog.text


def test_server_inventory_values_are_escaped_in_logs(
    discovery: ModuleType, monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture
) -> None:
    """Keep server-controlled newlines from forging log records."""
    inventory = [{"id": "id\nforged", "alias": "alias\nforged", "display_name": "name\nforged"}]
    monkeypatch.setattr(discovery, "list_quantum_computers", Mock(return_value=inventory))
    monkeypatch.setattr(discovery, "open_device", Mock(side_effect=RuntimeError))
    caplog.set_level(logging.INFO)
    with pytest.raises(SystemExit, match="Could not query properties"):
        discovery.run_discovery(base_url="https://example.com", token=None, tokens_file=None, min_qubits=5)
    messages = [record.getMessage() for record in caplog.records]
    assert any(
        r"id='id\nforged'" in message
        and r"alias='alias\nforged'" in message
        and r"display_name='name\nforged'" in message
        for message in messages
    )
    assert all("\nforged" not in message for message in messages)


@pytest.mark.parametrize("base_url", ["example.com", "ftp://example.com"])
def test_base_url_requires_http_scheme_and_host(discovery: ModuleType, base_url: str) -> None:
    """Reject malformed endpoints before logging or sending requests."""
    with pytest.raises(ValueError, match="HTTP or HTTPS scheme and a hostname"):
        discovery.resolve_base_url(base_url)


@pytest.mark.parametrize(
    ("base_url", "message"),
    [
        ("https://example.com:abc", "valid HTTP or HTTPS URL"),
        ("https://example.com/path with spaces", "valid HTTP or HTTPS URL"),
        (" https://example.com", "valid HTTP or HTTPS URL"),
        (r"https:\\example.com", "valid HTTP or HTTPS URL"),
        ("https://example.com?token=secret", "query or fragment"),
        ("https://example.com#fragment", "query or fragment"),
    ],
)
def test_base_url_rejects_invalid_port_query_and_fragment(discovery: ModuleType, base_url: str, message: str) -> None:
    """Reject malformed or ambiguous endpoints before they can reach a request or device."""
    with pytest.raises(ValueError, match=message) as exc_info:
        discovery.resolve_base_url(base_url)
    assert "secret" not in str(exc_info.value)


@pytest.mark.parametrize("base_url", ["http://example.com", "http://192.0.2.1"])
def test_bearer_token_requires_tls(discovery: ModuleType, base_url: str) -> None:
    """Never send bearer credentials to a non-loopback HTTP endpoint."""
    with pytest.raises(ValueError, match="HTTPS"):
        discovery.list_quantum_computers(base_url, EXAMPLE_VALUE)


def test_loopback_http_is_allowed(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Allow local HTTP test servers without weakening remote token transport."""
    response = Mock()
    response.json.return_value = {"quantum_computers": [{"id": "qc-1", "alias": "QC 1"}]}
    get = Mock(return_value=response)
    monkeypatch.setattr(discovery.requests, "get", get)
    discovery.list_quantum_computers("http://127.0.0.1:5000", EXAMPLE_VALUE)
    get.assert_called_once()


def test_validation_error_is_reported(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Keep actionable, safe configuration errors visible to the user."""
    monkeypatch.setenv("IQM_TOKEN", EXAMPLE_VALUE)
    monkeypatch.setenv("IQM_TOKENS_FILE", "tokens.json")
    monkeypatch.setattr(sys, "argv", ["discover_backends.py"])
    with pytest.raises(SystemExit) as exc_info:
        discovery.main()
    assert "Provide only one" in str(exc_info.value)
    assert EXAMPLE_VALUE not in str(exc_info.value)


def test_invalid_server_response_is_reported(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Print expected server shape errors without swallowing programming TypeErrors."""
    response = Mock()
    response.json.return_value = {}
    monkeypatch.setattr(discovery.requests, "get", Mock(return_value=response))
    monkeypatch.setattr(sys, "argv", ["discover_backends.py"])
    with pytest.raises(SystemExit, match="Expected a quantum_computers array"):
        discovery.main()


def test_unexpected_type_error_is_not_hidden(discovery: ModuleType, monkeypatch: pytest.MonkeyPatch) -> None:
    """Let programming errors keep their traceback instead of looking like network failures."""
    monkeypatch.setattr(sys, "argv", ["discover_backends.py"])
    monkeypatch.setattr(discovery, "run_discovery", Mock(side_effect=TypeError("bug")))
    with pytest.raises(TypeError, match="bug"):
        discovery.main()
