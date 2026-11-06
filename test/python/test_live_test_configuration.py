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

"""Check the live test fixture's environment handling without a provider."""

from __future__ import annotations

import pytest
import test_device_capabilities as live_tests


@pytest.mark.parametrize(
    ("canonical", "legacy", "expected"),
    [("canonical", "legacy", "canonical"), ("", "legacy", "legacy")],
)
def test_live_device_fixture_prefers_canonical_environment(
    monkeypatch: pytest.MonkeyPatch, canonical: str, legacy: str, expected: str
) -> None:
    """Both sessions use the same selector and endpoint precedence."""
    monkeypatch.setenv("IQM_SERVER_URL", canonical)
    monkeypatch.setenv("IQM_BASE_URL", legacy)
    monkeypatch.setenv("IQM_QUANTUM_COMPUTER", canonical)
    monkeypatch.setenv("IQM_QC_ALIAS", legacy)
    monkeypatch.delenv("IQM_QC_ID", raising=False)
    monkeypatch.delenv("IQM_TOKEN", raising=False)
    monkeypatch.delenv("IQM_TOKENS_FILE", raising=False)
    captured = {}

    def capture(*_args: object, **kwargs: object) -> object:
        captured.update(kwargs)
        return object()

    monkeypatch.setattr(live_tests, "open_device", capture)
    live_tests.device.__wrapped__(object())  # ty: ignore[unresolved-attribute]
    assert captured["base_url"] == expected
    assert captured["custom2"] == expected
