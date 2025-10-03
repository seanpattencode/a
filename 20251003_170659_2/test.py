#!/usr/bin/env python3
import contextlib
import io
import runpy
import sys
from pathlib import Path


def run_script(path: Path, argument: str) -> str:
    original_argv = sys.argv.copy()
    sys.argv = [path.name, argument]
    buffer = io.StringIO()
    try:
        with contextlib.redirect_stdout(buffer):
            runpy.run_path(str(path), run_name="__main__")
    finally:
        sys.argv = original_argv

    return buffer.getvalue().strip()


def main() -> None:
    base = Path(__file__).parent
    factor_output = run_script(base / "factor.py", "84")
    factor2_output = run_script(base / "factor2.py", "84")
    print(f"factor.py output: {factor_output}")
    print(f"factor2.py output: {factor2_output}")


if __name__ == "__main__":
    main()
