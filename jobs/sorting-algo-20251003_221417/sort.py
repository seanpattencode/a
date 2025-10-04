"""Quicksort implementation for sorting iterables."""
from __future__ import annotations

from typing import Iterable, List, TypeVar

T = TypeVar("T")


def quicksort(items: Iterable[T]) -> List[T]:
    """Return a new list containing the sorted items using quicksort."""
    sequence = list(items)
    if len(sequence) <= 1:
        return sequence

    pivot = sequence[len(sequence) // 2]
    less = [item for item in sequence if item < pivot]
    equal = [item for item in sequence if item == pivot]
    greater = [item for item in sequence if item > pivot]

    return quicksort(less) + equal + quicksort(greater)


if __name__ == "__main__":
    SAMPLE_DATA = [5, 3, 8, 4, 2, 7, 1, 10]
    print(quicksort(SAMPLE_DATA))
