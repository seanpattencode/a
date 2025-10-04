#!/usr/bin/env python3
import sys

from sympy import factorint


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("Usage: factor2.py <integer>")
    n = int(sys.argv[1])
    factors_map = factorint(n)
    factors = []
    for prime in sorted(factors_map):
        factors.extend([prime] * factors_map[prime])
    print(*factors if factors else [n])


if __name__ == "__main__":
    main()
