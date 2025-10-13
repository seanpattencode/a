"""Quicksort implementation with a simple Lomuto partition scheme."""
from __future__ import annotations

from typing import Iterable, List, TypeVar

T = TypeVar("T")


def quicksort(values: Iterable[T]) -> List[T]:
    """Return a new list containing the items from ``values`` in ascending order."""
    arr = list(values)
    _quicksort(arr, 0, len(arr) - 1)
    return arr


def _quicksort(arr: List[T], low: int, high: int) -> None:
    while low < high:
        pivot_index = _partition(arr, low, high)
        if pivot_index - low < high - pivot_index:
            _quicksort(arr, low, pivot_index - 1)
            low = pivot_index + 1
        else:
            _quicksort(arr, pivot_index + 1, high)
            high = pivot_index - 1


def _partition(arr: List[T], low: int, high: int) -> int:
    pivot = arr[high]
    i = low - 1
    for j in range(low, high):
        if arr[j] <= pivot:
            i += 1
            arr[i], arr[j] = arr[j], arr[i]
    arr[i + 1], arr[high] = arr[high], arr[i + 1]
    return i + 1


if __name__ == "__main__":
    sample = [5, 3, 8, 4, 2, 7, 1, 10]
    print(quicksort(sample))
