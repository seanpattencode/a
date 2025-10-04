import sys

from sympy import factorint


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("Usage: factor2.py <integer>")

    try:
        n = int(sys.argv[1])
    except ValueError as exc:
        raise SystemExit("Input must be an integer") from exc

    if n <= 1:
        print("")
        return

    factors = factorint(n, multiple=True)
    print(" * ".join(map(str, factors)))


if __name__ == "__main__":
    main()
