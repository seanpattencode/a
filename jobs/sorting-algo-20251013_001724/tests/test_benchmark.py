import random

from sort import quicksort


def test_quicksort_benchmark(benchmark):
    rng = random.Random(0)
    data = [rng.random() for _ in range(1000)]

    result = benchmark(lambda: quicksort(list(data)))

    assert result == sorted(data)
