import sys

from sympy import factorint


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: python factor2.py <integer>")

    value = int(sys.argv[1])
    if value == 1:
        print("1")
        return

    factors = []
    for prime, exponent in factorint(value).items():
        factors.extend([prime] * exponent)

    print("*".join(str(factor) for factor in factors))


if __name__ == "__main__":
    main()
