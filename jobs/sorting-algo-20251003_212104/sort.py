"""Quicksort implementation."""

from __future__ import annotations

from typing import Iterable, List, Sequence, TypeVar


T = TypeVar("T")


def quicksort(values: Sequence[T]) -> List[T]:
    """Return a new list containing the items from ``values`` sorted ascending."""
    items = list(values)
    if len(items) <= 1:
        return items

    pivot = items[len(items) // 2]
    less = [value for value in items if value < pivot]
    equal = [value for value in items if value == pivot]
    greater = [value for value in items if value > pivot]

    return quicksort(less) + equal + quicksort(greater)


def sort(values: Iterable[T]) -> List[T]:
    """Helper that sorts any iterable by routing through ``quicksort``."""
    return quicksort(list(values))
