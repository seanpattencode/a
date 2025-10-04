import random
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from sort import quicksort


def test_quicksort_benchmark_1000_random_numbers():
    random.seed(42)
    data = [random.randint(-10_000, 10_000) for _ in range(1000)]

    start = time.perf_counter()
    result = quicksort(data)
    elapsed = time.perf_counter() - start

    assert result == sorted(data)
    assert elapsed < 0.05, f"quicksort took too long: {elapsed:.4f}s"
