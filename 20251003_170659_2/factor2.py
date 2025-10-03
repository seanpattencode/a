#!/usr/bin/env python3
import sys
from itertools import chain

from sympy import factorint


def main() -> None:
    number = int(sys.argv[1])
    factors = factorint(number)
    ordered = chain.from_iterable([prime] * exponent for prime, exponent in sorted(factors.items()))
    print(*ordered)


if __name__ == "__main__":
    main()
