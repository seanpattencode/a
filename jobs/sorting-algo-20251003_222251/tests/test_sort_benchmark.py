import random
import time

from sort import quicksort


def test_quicksort_benchmark_1000_random_numbers():
    rand = random.Random(0)
    data = [rand.randint(-1_000_000, 1_000_000) for _ in range(1_000)]

    start = time.perf_counter()
    result = quicksort(data)
    duration = time.perf_counter() - start

    assert result == sorted(data)
    assert duration < 0.5, f"Quicksort took {duration:.6f}s for 1000 items"
