#!/usr/bin/env python3
import subprocess
import sys
import time
import os

WHITELIST_5SEC = {
    'wiki_fetcher', 'scraper', 'gdrive', 'curl', 'wget', 'git', 'npm', 'pip'
}

WHITELIST_FOREVER = {
    'web.py', 'aios_api.py', 'scheduler.py', 'poll', 'watch', 'serve'
}

def get_timeout(cmd):
    cmd_str = ' '.join(cmd).lower()

    for pattern in WHITELIST_FOREVER:
        if pattern in cmd_str:
            return None

    for pattern in WHITELIST_5SEC:
        if pattern in cmd_str:
            return 5.0

    return float(os.environ.get('AIOS_TIMEOUT', '0.1'))

timeout = get_timeout(sys.argv[1:])

if timeout is None:
    result = subprocess.run(sys.argv[1:], capture_output=True, text=True)
    print(result.stdout)
    result.stderr and print(result.stderr, file=sys.stderr)
    sys.exit(result.returncode)
else:
    start = time.time()
    try:
        result = subprocess.run(sys.argv[1:], capture_output=True, text=True, timeout=timeout)
        elapsed = time.time() - start
        print(result.stdout)
        result.stderr and print(result.stderr, file=sys.stderr)
        elapsed > timeout * 0.8 and print(f"WARNING: Process took {elapsed:.3f}s (limit: {timeout}s)", file=sys.stderr)
        sys.exit(result.returncode)
    except subprocess.TimeoutExpired:
        elapsed = time.time() - start
        print(f"DEATH: Process killed after {elapsed:.3f}s timeout", file=sys.stderr)
        sys.exit(124)