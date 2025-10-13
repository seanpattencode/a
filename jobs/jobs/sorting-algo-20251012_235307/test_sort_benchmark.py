"""Benchmark tests for sort.quicksort."""

import random
import statistics
import time

import sort

_rng = random.Random(1337)
_DATASET = [_rng.randint(0, 1_000_000) for _ in range(1000)]


def test_quicksort_benchmark_1000_random_numbers() -> None:
    durations = []
    for _ in range(5):
        dataset = list(_DATASET)
        expected = sorted(dataset)
        start = time.perf_counter()
        result = sort.quicksort(dataset)
        duration = time.perf_counter() - start
        durations.append(duration)
        assert result == expected

    average_duration = statistics.mean(durations)
    # 1k elements should sort well under 100ms on typical hardware.
    assert average_duration < 0.1, (
        f"quicksort average runtime too slow: {average_duration:.6f}s"
    )
