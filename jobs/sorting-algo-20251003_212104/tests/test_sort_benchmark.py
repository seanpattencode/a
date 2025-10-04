import pathlib
import random
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from sort import sort


def test_sort_benchmark_1000_random_numbers():
    generator = random.Random(0)
    data = [generator.random() for _ in range(1000)]

    start = time.perf_counter()
    result = sort(data)
    duration = time.perf_counter() - start

    assert result == sorted(data)
    print(f"sort() completed in {duration:.6f} seconds for 1000 random numbers")
