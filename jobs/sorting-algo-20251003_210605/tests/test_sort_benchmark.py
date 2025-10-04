import random
import time

from sort import quicksort


def test_quicksort_benchmark_1000_random_numbers():
    rng = random.Random(0)  # ensure the data set is reproducible for consistent timings
    data = [rng.randint(-1_000_000, 1_000_000) for _ in range(1000)]

    start = time.perf_counter()
    result = quicksort(data)
    elapsed = time.perf_counter() - start

    assert result == sorted(data)
    assert elapsed < 1.0, f"Expected quicksort to finish within 1s, took {elapsed:.3f}s"
