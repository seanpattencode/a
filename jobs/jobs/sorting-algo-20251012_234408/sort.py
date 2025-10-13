"""Quicksort implementation with an exposed sort_list function."""

def quicksort(items):
    """Return a new list containing the sorted items using quicksort."""
    if len(items) < 2:
        return list(items)
    pivot = items[len(items) // 2]
    less = [x for x in items if x < pivot]
    equal = [x for x in items if x == pivot]
    greater = [x for x in items if x > pivot]
    return quicksort(less) + equal + quicksort(greater)


def sort_list(items):
    """Sort the provided iterable and return a new list."""
    return quicksort(list(items))


if __name__ == "__main__":
    sample = [3, 6, 8, 10, 1, 2, 1]
    print(sort_list(sample))
