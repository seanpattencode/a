"""QuickSort implementation in Python."""

from __future__ import annotations

from typing import List, Sequence, TypeVar

T = TypeVar("T")


def quicksort(values: Sequence[T]) -> List[T]:
    """Return a new list containing the elements of `values` sorted via quicksort.

    The implementation is not in-place; instead it returns a freshly allocated list
    to keep the input sequence unmodified.
    """
    values = list(values)
    if len(values) <= 1:
        return values

    pivot = values[0]
    less: List[T] = []
    equal: List[T] = []
    greater: List[T] = []

    for value in values:
        if value < pivot:
            less.append(value)
        elif value > pivot:
            greater.append(value)
        else:
            equal.append(value)

    return quicksort(less) + equal + quicksort(greater)


if __name__ == "__main__":
    sample = [5, 3, 8, 4, 2, 7, 1, 10]
    print(quicksort(sample))
