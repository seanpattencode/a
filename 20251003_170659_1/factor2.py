import sys
from typing import List

from sympy import factorint


def factorize(n: int) -> List[int]:
    """Return the prime factors of *n* using SymPy's factorint."""
    factor_map = factorint(n)
    factors: List[int] = []
    for prime in sorted(factor_map):
        factors.extend([int(prime)] * factor_map[prime])
    return factors


def main() -> None:
    args = sys.argv[1:]
    if args:
        value = int(args[0])
    else:
        value = int(sys.stdin.readline())
    print(factorize(value))


if __name__ == "__main__":
    main()
