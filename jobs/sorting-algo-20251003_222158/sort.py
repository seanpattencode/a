"""QuickSort implementation in Python."""
from __future__ import annotations

from typing import Iterable, List, TypeVar

T = TypeVar("T")


def quicksort(values: Iterable[T]) -> List[T]:
    """Return a new list containing the items from *values* in ascending order.

    The algorithm uses the classic quicksort strategy with a median pivot to
    limit the chance of worst-case behavior on already sorted inputs.
    """
    items = list(values)
    if len(items) < 2:
        return items

    pivot_index = len(items) // 2
    pivot = items[pivot_index]

    less: List[T] = []
    equal: List[T] = []
    greater: List[T] = []

    for item in items:
        if item < pivot:
            less.append(item)
        elif item > pivot:
            greater.append(item)
        else:
            equal.append(item)

    return quicksort(less) + equal + quicksort(greater)


if __name__ == "__main__":
    sample = [5, 3, 8, 4, 2, 7, 1, 10]
    print(quicksort(sample))
