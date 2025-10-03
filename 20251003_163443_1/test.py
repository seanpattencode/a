import importlib
import io
import sys
from contextlib import redirect_stdout


def run_factor_script(module_name: str, call_main: bool = False) -> str:
    buffer = io.StringIO()
    saved_argv = sys.argv
    sys.argv = [module_name, "84"]
    try:
        with redirect_stdout(buffer):
            module = importlib.import_module(module_name)
            if call_main:
                module.main()
    finally:
        sys.argv = saved_argv
        if module_name in sys.modules:
            del sys.modules[module_name]
    return buffer.getvalue().strip()


if __name__ == "__main__":
    factor_output = run_factor_script("factor")
    factor2_output = run_factor_script("factor2", call_main=True)
    print(f"factor.py -> {factor_output}")
    print(f"factor2.py -> {factor2_output}")
