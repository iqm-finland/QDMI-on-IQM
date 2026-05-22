# Copyright (c) 2025 - 2026 IQM Finland Oy
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://github.com/iqm-finland/QDMI-on-IQM/blob/main/LICENSE.md
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Inject latest stable release metadata into documentation sources."""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path
from typing import TYPE_CHECKING, Protocol

if TYPE_CHECKING:
    from collections.abc import Callable

STABLE_TAG_PATTERN = re.compile(r"^v(?P<version>\d+\.\d+\.\d+)$")


class _SphinxConfig(Protocol):
    release: str
    release_substitutions: dict[str, str]


class _SphinxApp(Protocol):
    confdir: str
    config: _SphinxConfig

    def add_config_value(self, name: str, default: dict[str, str], rebuild: str) -> None: ...

    def connect(self, event: str, callback: Callable[..., object]) -> None: ...


def _git_output(repo_root: Path, *args: str) -> str | None:
    try:
        return subprocess.check_output(  # noqa: S603 -- git command is not constructed from untrusted input
            ["git", *args],  # noqa: S607
            cwd=repo_root,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def _latest_release_metadata(repo_root: Path, fallback_release: str) -> dict[str, str]:
    env_version = os.environ.get("IQM_QDMI_LATEST_RELEASE_VERSION")
    env_commit = os.environ.get("IQM_QDMI_LATEST_RELEASE_COMMIT")
    if env_version and env_commit:
        normalized_version = env_version.removeprefix("v")
        return {
            "latest_release_version": normalized_version,
            "latest_release_tag": f"v{normalized_version}",
            "latest_release_commit": env_commit,
        }

    tags = _git_output(repo_root, "tag", "--list", "v*", "--sort=-v:refname")
    if tags:
        for tag in tags.splitlines():
            match = STABLE_TAG_PATTERN.fullmatch(tag)
            if match is None:
                continue

            commit = _git_output(repo_root, "rev-list", "-n", "1", tag)
            if commit:
                return {
                    "latest_release_version": match.group("version"),
                    "latest_release_tag": tag,
                    "latest_release_commit": commit,
                }

    return {
        "latest_release_version": fallback_release,
        "latest_release_tag": f"v{fallback_release}",
        "latest_release_commit": _git_output(repo_root, "rev-parse", "HEAD") or "",
    }


def _set_release_substitutions(app: _SphinxApp, config: _SphinxConfig) -> None:
    release = config.release.split("+")[0]
    repo_root = Path(app.confdir).resolve().parent
    config.release_substitutions = _latest_release_metadata(repo_root, release)


def _apply_release_substitutions(app: _SphinxApp, _docname: str, source: list[str]) -> None:
    text = source[0]
    for key, value in app.config.release_substitutions.items():
        text = text.replace(f"{{{{{key}}}}}", value)
    source[0] = text


def setup(app: _SphinxApp) -> dict[str, bool]:
    """Register the release placeholder substitutions for all source files.

    Returns:
        Extension metadata declaring the extension safe for parallel reading.
    """
    app.add_config_value("release_substitutions", {}, "env")
    app.connect("config-inited", _set_release_substitutions)
    app.connect("source-read", _apply_release_substitutions)
    return {"parallel_read_safe": True}
