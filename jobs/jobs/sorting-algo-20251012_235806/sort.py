"""Quicksort implementation for in-place sorting of mutable sequences."""
from __future__ import annotations

from typing import MutableSequence, TypeVar

T = TypeVar("T")


def quicksort(values: MutableSequence[T]) -> MutableSequence[T]:
    """Sort ``values`` in place using the quicksort algorithm and return it."""

    def partition(lo: int, hi: int) -> int:
        pivot = values[hi]
        i = lo - 1
        for j in range(lo, hi):
            if values[j] <= pivot:
                i += 1
                values[i], values[j] = values[j], values[i]
        values[i + 1], values[hi] = values[hi], values[i + 1]
        return i + 1

    def _quicksort(lo: int, hi: int) -> None:
        if lo >= hi:
            return
        pivot_index = partition(lo, hi)
        _quicksort(lo, pivot_index - 1)
        _quicksort(pivot_index + 1, hi)

    if not values:
        return values

    _quicksort(0, len(values) - 1)
    return values


if __name__ == "__main__":
    sample = [8, 3, 5, 4, 7, 6, 1, 2]
    quicksort(sample)
    print(sample)
