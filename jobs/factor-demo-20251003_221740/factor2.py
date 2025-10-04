import sys

from sympy import factorint


def main() -> None:
    n = int(sys.argv[1])
    factors_dict = factorint(n)
    factors = [prime for prime, exponent in factors_dict.items() for _ in range(exponent)]
    print(factors)


if __name__ == "__main__":
    main()
