#!/usr/bin/env python3
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True)
print(result.stdout)
print(result.stderr, file=sys.stderr)
sys.exit(result.returncode)