"""Simple quicksort implementation."""
from __future__ import annotations

from typing import Iterable, List, TypeVar

T = TypeVar("T")


def quicksort(items: Iterable[T]) -> List[T]:
    """Return a new list containing the sorted items using quicksort."""

    items = list(items)
    if len(items) <= 1:
        return items

    pivot_index = len(items) // 2
    pivot = items[pivot_index]

    left: List[T] = []
    middle: List[T] = []
    right: List[T] = []

    for item in items:
        if item < pivot:
            left.append(item)
        elif item > pivot:
            right.append(item)
        else:
            middle.append(item)

    # Recursively sort partitions around the pivot.
    return quicksort(left) + middle + quicksort(right)


if __name__ == "__main__":
    sample = [3, 6, 8, 10, 1, 2, 1]
    print(quicksort(sample))
