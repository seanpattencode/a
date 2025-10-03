import sys
from sympy import factorint


def read_input() -> int:
    if len(sys.argv) > 1:
        return int(sys.argv[1])
    return int(input())


def main() -> None:
    n = read_input()
    if n <= 1:
        print([1])
        return
    factors = []
    for prime, exponent in sorted(factorint(n).items()):
        factors.extend([prime] * exponent)
    print(factors)


if __name__ == "__main__":
    main()
