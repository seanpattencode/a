import runpy
import sys
from contextlib import redirect_stdout
from io import StringIO


def run_module(module_name: str, argument: str) -> str:
    saved_argv = sys.argv[:]
    buffer = StringIO()
    try:
        sys.argv = [module_name, argument]
        with redirect_stdout(buffer):
            runpy.run_module(module_name, run_name="__main__")
        result = buffer.getvalue().strip()
    finally:
        sys.argv = saved_argv
        buffer.close()
    return result


def main() -> None:
    for module in ("factor", "factor2"):
        output = run_module(module, "84")
        print(f"{module} output: {output}")


if __name__ == "__main__":
    main()
