import sys
from sympy import divisors


def factors(n: int):
    return divisors(n)


if __name__ == "__main__":
    print(factors(int(sys.argv[1])))
