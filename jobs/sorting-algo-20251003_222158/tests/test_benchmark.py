import pathlib
import random
import statistics
import sys
import time

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from sort import quicksort


def test_quicksort_benchmark_1000_random_numbers(record_property):
    random.seed(20251003)
    dataset = [random.randint(0, 10_000) for _ in range(1000)]
    expected = sorted(dataset)

    # Warm-up run helps separate import/setup cost from measurement.
    assert quicksort(dataset) == expected

    iteration_count = 5
    durations = []
    for _ in range(iteration_count):
        start = time.perf_counter()
        result = quicksort(dataset)
        durations.append(time.perf_counter() - start)
        assert result == expected

    average_duration = statistics.mean(durations)
    record_property(
        "quicksort_avg_seconds_1000_numbers",
        f"{average_duration:.6f}",
    )
    record_property(
        "quicksort_runs_seconds_1000_numbers",
        ", ".join(f"{duration:.6f}" for duration in durations),
    )
