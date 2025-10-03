#!/usr/bin/env python3
from itertools import chain, repeat
import sys

from sympy import factorint


def factorize(value: int) -> list[int]:
    """Return the prime factors of ``value`` in ascending order."""
    factors = factorint(value)
    ordered_pairs = sorted(factors.items())
    expanded = chain.from_iterable(repeat(prime, power) for prime, power in ordered_pairs)
    return list(expanded)


def main(args: list[str] | None = None) -> None:
    arguments = sys.argv[1:] if args is None else args
    if not arguments:
        raise SystemExit("Usage: factor2.py <integer>")
    try:
        number = int(arguments[0])
    except ValueError as exc:  # pragma: no cover - defensive
        raise SystemExit("Input must be an integer") from exc
    print(*factorize(number), sep=" ")


if __name__ == "__main__":
    main()
