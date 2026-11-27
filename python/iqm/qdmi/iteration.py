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

"""Manage IQM iterations independently of a device session."""

from __future__ import annotations

import ctypes
import json
import os
from dataclasses import dataclass
from functools import cache
from threading import Lock
from typing import TYPE_CHECKING, Self
from uuid import UUID
from weakref import finalize

from . import IQM_QDMI_LIBRARY_PATH

if TYPE_CHECKING:
    from types import TracebackType

__all__ = ["IterationClient", "IterationError", "IterationResult"]

_DEFAULT_TIMEOUT_MS = 30_000
_MAX_TIMEOUT_MS = 2**64 - 1


@dataclass(frozen=True)
class IterationResult:
    """Reservation ID, server state, and HTTP status from an operation.

    The ID or state can be absent in a partial result. An HTTP status of zero
    means no response was received.
    """

    iteration_id: str | None
    state: str | None
    http_status: int


class IterationError(RuntimeError):
    """Native failure with its QDMI status, partial result, and diagnostics."""

    def __init__(
        self, operation: str, qdmi_status: int, result: IterationResult, diagnostics: dict[str, str | int | bool]
    ) -> None:
        """Keep recovery information even when creation fails."""
        message = f"IQM iteration {operation} failed (QDMI status {qdmi_status}, HTTP status {result.http_status})"
        if detail := diagnostics.get("message"):
            message += f". {detail}"
        super().__init__(message)
        self.qdmi_status = qdmi_status
        self.result = result
        self.diagnostics = diagnostics


class _NativeResult(ctypes.Structure):
    """Match the caller-owned result in the installed iteration.h."""

    _fields_ = [("iteration_id", ctypes.c_char * 37), ("state", ctypes.c_char * 16), ("http_status", ctypes.c_int)]


@cache
def _load_library() -> ctypes.CDLL:
    lib = ctypes.CDLL(str(IQM_QDMI_LIBRARY_PATH))
    lib.IQM_QDMI_iteration_client_alloc.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    lib.IQM_QDMI_iteration_client_alloc.restype = ctypes.c_int
    lib.IQM_QDMI_iteration_client_free.argtypes = [ctypes.c_void_p]
    lib.IQM_QDMI_iteration_client_free.restype = None
    lib.IQM_QDMI_iteration_client_diagnostics.argtypes = [ctypes.c_void_p]
    lib.IQM_QDMI_iteration_client_diagnostics.restype = ctypes.c_char_p
    lib.IQM_QDMI_iteration_create.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint64,
        ctypes.POINTER(_NativeResult),
    ]
    lib.IQM_QDMI_iteration_create.restype = ctypes.c_int
    for function in (lib.IQM_QDMI_iteration_get, lib.IQM_QDMI_iteration_end):
        function.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64, ctypes.POINTER(_NativeResult)]
        function.restype = ctypes.c_int
    return lib


def _check_range(name: str, value: int, minimum: int, maximum: int) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        msg = f"{name} must be an integer between {minimum} and {maximum}"
        raise ValueError(msg)


def _encode(value: str | None) -> bytes | None:
    if value is None:
        return None
    encoded = value.encode()
    if b"\0" in encoded:
        msg = "IQM iteration arguments cannot contain NUL characters"
        raise ValueError(msg)
    return encoded


class IterationClient:
    """Create, inspect, and end IQM reservations without a device session.

    Args:
        base_url: IQM server URL. Defaults to `IQM_SERVER_URL`, then `IQM_BASE_URL`.
        token: Optional authentication token.
        tokens_file: Optional authentication file. If neither credential is
            provided, the native client uses `IQM_TOKEN` or `IQM_TOKENS_FILE`.
        request_timeout_ms: Upper bound in milliseconds for each HTTP request.

    Calls on one client are serialized. Closing the client releases its
    native resources. Call `end()` explicitly to end a reservation.
    """

    def __init__(
        self,
        *,
        base_url: str | None = None,
        token: str | None = None,
        tokens_file: str | os.PathLike[str] | None = None,
        request_timeout_ms: int = _DEFAULT_TIMEOUT_MS,
    ) -> None:
        """Allocate a management client without making a network request.

        Raises:
            IterationError: The native client could not be allocated.
        """
        _check_range("request_timeout_ms", request_timeout_ms, 1, _MAX_TIMEOUT_MS)
        self._lock = Lock()
        self._lib = _load_library()
        self._handle = ctypes.c_void_p()
        status = self._lib.IQM_QDMI_iteration_client_alloc(
            _encode(base_url),
            _encode(token),
            _encode(os.fspath(tokens_file) if tokens_file is not None else None),
            request_timeout_ms,
            ctypes.byref(self._handle),
        )
        if status:
            msg = "Check the server URL, timeout, and authentication settings."
            operation = "client allocation"
            raise IterationError(operation, status, IterationResult(None, None, 0), {"message": msg})
        self._free = finalize(self, self._lib.IQM_QDMI_iteration_client_free, self._handle)

    def __enter__(self) -> Self:
        """Return the open client."""
        with self._lock:
            self._require_open()
        return self

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, traceback: TracebackType | None
    ) -> None:
        """Release native resources on context exit."""
        self.close()

    def close(self) -> None:
        """Release the client handle. Reservations remain on the server."""
        with self._lock:
            self._free()

    def _require_open(self) -> None:
        if not self._free.alive:
            msg = "The IQM iteration client is closed"
            raise RuntimeError(msg)

    def _call(self, operation: str, *args: bytes | int | None, timeout_ms: int) -> IterationResult:
        _check_range("timeout_ms", timeout_ms, 1, _MAX_TIMEOUT_MS)
        with self._lock:
            self._require_open()
            native = _NativeResult()
            function = getattr(self._lib, f"IQM_QDMI_iteration_{operation}")
            status = function(self._handle, *args, timeout_ms, ctypes.byref(native))
            result = IterationResult(
                native.iteration_id.decode() or None, native.state.decode() or None, native.http_status
            )
            if status:
                raw = self._lib.IQM_QDMI_iteration_client_diagnostics(self._handle)
                raise IterationError(operation, status, result, json.loads(raw) if raw else {})
            return result

    def create(
        self,
        qc: str | UUID,
        max_duration_seconds: int,
        *,
        idle_timeout_seconds: int = 0,
        timeout_ms: int = _DEFAULT_TIMEOUT_MS,
    ) -> IterationResult:
        """Request an iteration for a quantum computer UUID or alias.

        If an `IterationError` carries an ID in its partial result, keep the
        ID so you can end the reservation.

        Args:
            qc: Quantum computer UUID or alias.
            max_duration_seconds: Reservation limit after activation, from 1 to 2700 seconds.
            idle_timeout_seconds: Idle limit up to the reservation limit, or zero for the server default.
            timeout_ms: Total time budget for the native operation in milliseconds.

        Returns:
            The confirmed iteration ID and its current state.

        Raises:
            ValueError: A selector or limit is invalid.
        """
        selector = str(qc)
        if not selector:
            msg = "qc must be a quantum computer UUID or alias"
            raise ValueError(msg)
        _check_range("max_duration_seconds", max_duration_seconds, 1, 2700)
        _check_range("idle_timeout_seconds", idle_timeout_seconds, 0, max_duration_seconds)
        return self._call(
            "create", _encode(selector), max_duration_seconds, idle_timeout_seconds, timeout_ms=timeout_ms
        )

    def get(self, iteration_id: str | UUID, *, timeout_ms: int = _DEFAULT_TIMEOUT_MS) -> IterationResult:
        """Read a reservation's current state within the given millisecond budget.

        Returns:
            The iteration ID and its current state.
        """
        return self._call("get", str(UUID(str(iteration_id))).encode(), timeout_ms=timeout_ms)

    def end(self, iteration_id: str | UUID, *, timeout_ms: int = _DEFAULT_TIMEOUT_MS) -> IterationResult:
        """Request termination within the given millisecond budget.

        Returns:
            The current state and HTTP status. A status of 202 acknowledges
            the request. Check the state with `get()` to confirm termination.
        """
        return self._call("end", str(UUID(str(iteration_id))).encode(), timeout_ms=timeout_ms)
