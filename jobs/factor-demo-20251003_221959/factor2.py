import sys

from sympy import factorint


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: factor2.py <number>")
    n = int(sys.argv[1])
    factors = factorint(n)
    expanded = []
    for prime, exponent in sorted(factors.items()):
        expanded.extend([str(prime)] * exponent)
    print(" ".join(expanded) or str(n))


if __name__ == "__main__":
    main()
