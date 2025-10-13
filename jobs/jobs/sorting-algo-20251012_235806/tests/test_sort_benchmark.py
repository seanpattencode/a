import random
import sys
import time
from pathlib import Path

# Ensure the project root is on sys.path so we can import the sort module.
ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from sort import quicksort


def test_quicksort_benchmark_1000_random_numbers(record_property):
    """Benchmark quicksort with 1000 random integers and verify correctness."""
    rng = random.Random(0)
    baseline = [rng.randint(-10_000, 10_000) for _ in range(1000)]
    data = list(baseline)

    start = time.perf_counter()
    quicksort(data)
    duration = time.perf_counter() - start

    # Surface the runtime in pytest reports so the benchmark is captured without flaky thresholds.
    record_property("quicksort_runtime_seconds", duration)

    assert data == sorted(baseline)
