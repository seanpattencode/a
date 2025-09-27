#!/usr/bin/env python3
import subprocess
import sys
import time
import os

DEATH_TIMEOUT = float(os.environ.get('AIOS_TIMEOUT', '0.1'))

start = time.time()
try:
    result = subprocess.run(sys.argv[1:], capture_output=True, text=True, timeout=DEATH_TIMEOUT)
    elapsed = time.time() - start
    print(result.stdout)
    result.stderr and print(result.stderr, file=sys.stderr)
    elapsed > DEATH_TIMEOUT * 0.8 and print(f"WARNING: Process took {elapsed:.3f}s (limit: {DEATH_TIMEOUT}s)", file=sys.stderr)
    sys.exit(result.returncode)
except subprocess.TimeoutExpired:
    elapsed = time.time() - start
    print(f"DEATH: Process killed after {elapsed:.3f}s timeout", file=sys.stderr)
    sys.exit(124)