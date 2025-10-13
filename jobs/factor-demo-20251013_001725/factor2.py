import sys
from sympy import factorint


def main() -> None:
    print(*factorint(int(sys.argv[1]), multiple=True))


if __name__ == "__main__":
    main()
