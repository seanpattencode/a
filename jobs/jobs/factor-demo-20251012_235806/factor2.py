import sys
from sympy import factorint


def _read_target() -> int:
    if len(sys.argv) > 1:
        return int(sys.argv[1])
    return int(sys.stdin.readline().strip())


def main() -> None:
    n = _read_target()
    factors = factorint(n, multiple=True)
    print(*factors)


if __name__ == "__main__":
    main()
