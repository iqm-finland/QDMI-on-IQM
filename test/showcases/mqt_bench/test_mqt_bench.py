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

"""MQT Bench showcases using benchmark circuits."""

from __future__ import annotations

import os
from typing import TYPE_CHECKING

import pytest
from qiskit.quantum_info import hellinger_fidelity
from support import sample_counts, skip_if_backend_too_small

if TYPE_CHECKING:
    from collections.abc import Callable

    from mqt.core.plugins.qiskit.backend import QDMIBackend

pytest.importorskip(
    "mqt.bench",
    reason="Install the showcase dependencies with `uv run --group showcase ...` to run the MQT Bench showcases.",
)
from mqt.bench import BenchmarkLevel, get_benchmark

MQT_BENCH_SHOTS = int(os.getenv("IQM_MQT_BENCH_SHOTS", "1024"))

GHZ_FIDELITY_THRESHOLD = 0.35
DEUTSCH_JOZSA_MAX_ZERO_PROBABILITY = 0.35
QFT_MIN_STATES = 3
GRAPHSTATE_MIN_STATES = 2
WSTATE_FIDELITY_THRESHOLD = 0.4


def validate_ghz_state_fidelity(counts: dict[str, int], *, num_qubits: int, threshold: float) -> None:
    """Validate that a GHZ-state measurement shows the expected parity pattern.

    Args:
        counts: Observed bitstring counts from the sampler.
        num_qubits: Number of qubits in the GHZ circuit.
        threshold: Minimum accepted Hellinger fidelity.
    """
    total_shots = sum(counts.values())
    expected_dist = {
        "0" * num_qubits: total_shots // 2,
        "1" * num_qubits: total_shots // 2,
    }
    fidelity = hellinger_fidelity(counts, expected_dist)
    assert fidelity >= threshold, f"GHZ-state fidelity {fidelity:.2%} below threshold {threshold:.2%}."


def validate_wstate_fidelity(counts: dict[str, int], *, num_qubits: int, threshold: float) -> None:
    """Validate that a W-state measurement emphasizes single-excitation outcomes.

    Args:
        counts: Observed bitstring counts from the sampler.
        num_qubits: Number of qubits in the W-state circuit.
        threshold: Minimum accepted Hellinger fidelity.
    """
    total_shots = sum(counts.values())
    expected_states = ["0" * index + "1" + "0" * (num_qubits - index - 1) for index in range(num_qubits)]
    expected_count_per_state = total_shots // num_qubits
    expected_dist = dict.fromkeys(expected_states, expected_count_per_state)
    fidelity = hellinger_fidelity(counts, expected_dist)
    assert fidelity >= threshold, f"W-state fidelity {fidelity:.2%} below threshold {threshold:.2%}."


def validate_deutsch_jozsa(counts: dict[str, int], *, max_zero_probability: float) -> None:
    """Validate that the balanced Deutsch-Jozsa circuit avoids the all-zero outcome.

    Args:
        counts: Observed bitstring counts from the sampler.
        max_zero_probability: Largest accepted probability for the all-zero outcome.
    """
    total_shots = sum(counts.values())
    all_zeros = "0" * len(next(iter(counts)))
    zero_probability = counts.get(all_zeros, 0) / total_shots
    assert zero_probability <= max_zero_probability, (
        f"Balanced Deutsch-Jozsa should not collapse to all zeros with probability {zero_probability:.2%}."
    )


def validate_qft(counts: dict[str, int], *, min_states: int) -> None:
    """Validate that QFT yields a diverse measurement distribution.

    Args:
        counts: Observed bitstring counts from the sampler.
        min_states: Minimum number of distinct bitstrings expected.
    """
    assert len(counts) >= min_states, f"QFT should expose at least {min_states} states, got {len(counts)}."


def validate_graphstate(counts: dict[str, int], *, min_states: int) -> None:
    """Validate that graph-state generation produces more than one outcome.

    Args:
        counts: Observed bitstring counts from the sampler.
        min_states: Minimum number of distinct bitstrings expected.
    """
    assert len(counts) >= min_states, f"Graph-state circuit should expose at least {min_states} states."


def run_mqt_bench_showcase(
    backend: QDMIBackend,
    benchmark: str,
    circuit_size: int,
    validation_func: Callable[[dict[str, int]], None],
    *,
    shots: int = MQT_BENCH_SHOTS,
) -> None:
    """Generate, execute, and validate an MQT Bench circuit.

    Args:
        backend: Backend that should execute the generated circuit.
        benchmark: Name of the benchmark family to generate.
        circuit_size: Number of qubits used for the benchmark circuit.
        validation_func: Validation callback applied to the observed counts.
        shots: Number of shots used for sampling.
    """
    skip_if_backend_too_small(backend, required_qubits=circuit_size)
    circuit = get_benchmark(
        benchmark=benchmark,
        level=BenchmarkLevel.ALG,
        circuit_size=circuit_size,
    )
    counts = sample_counts(backend, circuit, shots=shots)
    assert sum(counts.values()) == shots
    validation_func(counts)


@pytest.mark.mqt_bench
def test_ghz_showcase(backend: QDMIBackend) -> None:
    """Run the GHZ-state showcase through the backend sampler."""
    run_mqt_bench_showcase(
        backend,
        benchmark="ghz",
        circuit_size=3,
        validation_func=lambda counts: validate_ghz_state_fidelity(
            counts,
            num_qubits=3,
            threshold=GHZ_FIDELITY_THRESHOLD,
        ),
    )


@pytest.mark.mqt_bench
def test_deutsch_jozsa_showcase(backend: QDMIBackend) -> None:
    """Run the balanced Deutsch-Jozsa showcase through the backend sampler."""
    run_mqt_bench_showcase(
        backend,
        benchmark="dj",
        circuit_size=4,
        validation_func=lambda counts: validate_deutsch_jozsa(
            counts,
            max_zero_probability=DEUTSCH_JOZSA_MAX_ZERO_PROBABILITY,
        ),
    )


@pytest.mark.mqt_bench
def test_qft_showcase(backend: QDMIBackend) -> None:
    """Run the QFT showcase through the backend sampler."""
    run_mqt_bench_showcase(
        backend,
        benchmark="qft",
        circuit_size=3,
        validation_func=lambda counts: validate_qft(counts, min_states=QFT_MIN_STATES),
    )


@pytest.mark.mqt_bench
def test_graphstate_showcase(backend: QDMIBackend) -> None:
    """Run the graph-state showcase through the backend sampler."""
    run_mqt_bench_showcase(
        backend,
        benchmark="graphstate",
        circuit_size=4,
        validation_func=lambda counts: validate_graphstate(counts, min_states=GRAPHSTATE_MIN_STATES),
    )


@pytest.mark.mqt_bench
def test_wstate_showcase(backend: QDMIBackend) -> None:
    """Run the W-state showcase through the backend sampler."""
    run_mqt_bench_showcase(
        backend,
        benchmark="wstate",
        circuit_size=3,
        validation_func=lambda counts: validate_wstate_fidelity(
            counts,
            num_qubits=3,
            threshold=WSTATE_FIDELITY_THRESHOLD,
        ),
    )
