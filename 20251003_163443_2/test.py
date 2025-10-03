#!/usr/bin/env python3
import io
import sys
from contextlib import redirect_stdout

import factor2


def run_factor(argument: int) -> str:
    module_name = "factor"
    capture = io.StringIO()
    original_argv = sys.argv[:]
    sys.modules.pop(module_name, None)
    sys.argv = ["factor.py", str(argument)]
    try:
        with redirect_stdout(capture):
            __import__(module_name)
    finally:
        sys.argv = original_argv
        sys.modules.pop(module_name, None)
    return capture.getvalue().strip()


def run_factor2(argument: int) -> str:
    return " ".join(map(str, factor2.factorize(argument)))


if __name__ == "__main__":
    target = 84
    print(f"factor.py output: {run_factor(target)}")
    print(f"factor2.py output: {run_factor2(target)}")
