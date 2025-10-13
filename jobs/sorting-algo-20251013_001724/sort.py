"""Quicksort implementation."""

from typing import List, Sequence, TypeVar

T = TypeVar("T")


def quicksort(values: Sequence[T]) -> List[T]:
    """Return a sorted list of values using the quicksort algorithm."""
    if len(values) <= 1:
        return list(values)

    pivot = values[len(values) // 2]
    less = [v for v in values if v < pivot]
    equal = [v for v in values if v == pivot]
    greater = [v for v in values if v > pivot]

    return quicksort(less) + equal + quicksort(greater)
