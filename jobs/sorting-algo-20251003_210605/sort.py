"""Quicksort implementation.

This module exposes `quicksort`, which returns a new sorted list from
an iterable of comparable values.
"""
from typing import Iterable, List, MutableSequence, TypeVar


T = TypeVar("T")


def quicksort(values: Iterable[T]) -> List[T]:
    """Return a new list containing the sorted items from *values*.

    The implementation copies the input into a list before sorting, so the
    original iterable remains unchanged.
    """
    items = list(values)
    if len(items) < 2:
        return items

    _quicksort(items, 0, len(items) - 1)
    return items


def _quicksort(items: MutableSequence[T], low: int, high: int) -> None:
    stack = [(low, high)]
    while stack:
        low, high = stack.pop()
        if low >= high:
            continue

        pivot_index = _partition(items, low, high)

        left_size = pivot_index - 1 - low
        right_size = high - (pivot_index + 1)

        if left_size > 0:
            stack.append((low, pivot_index - 1))
        if right_size > 0:
            stack.append((pivot_index + 1, high))


def _partition(items: MutableSequence[T], low: int, high: int) -> int:
    pivot = items[high]
    i = low
    for j in range(low, high):
        if items[j] <= pivot:
            items[i], items[j] = items[j], items[i]
            i += 1
    items[i], items[high] = items[high], items[i]
    return i


if __name__ == "__main__":
    sample = [5, 3, 8, 3, 9, 1, 0]
    print(quicksort(sample))
