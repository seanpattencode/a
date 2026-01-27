#!/usr/bin/env python3
"""Test aio timing overhead vs direct python execution

Measures:
- Direct python execution time
- aio wrapper overhead
- Overhead as percentage
- Scaling with script duration
"""
import subprocess as sp, tempfile, os, statistics

AIO = os.path.expanduser("~/projects/aio/aio.py")
N = 5  # runs per test

def time_cmd(cmd):
    """Return execution time in ms"""
    times = []
    for _ in range(N):
        r = sp.run(cmd, shell=True, capture_output=True, text=True)
        # Parse 'real' time from time command output
        import re
        m = re.search(r'(\d+\.\d+) total', r.stderr) or re.search(r'real\s+0m([\d.]+)s', r.stderr)
        if m: times.append(float(m.group(1)) * 1000)
    return statistics.mean(times) if times else 0, statistics.stdev(times) if len(times) > 1 else 0

def test_overhead():
    scripts = [
        ("noop", "pass"),
        ("print", "print('x')"),
        ("sleep_10ms", "import time; time.sleep(0.01)"),
        ("sleep_100ms", "import time; time.sleep(0.1)"),
        ("sleep_500ms", "import time; time.sleep(0.5)"),
        ("import_json", "import json; json.dumps({'a':1})"),
        ("import_heavy", "import subprocess, sqlite3, json, re, os, sys"),
    ]

    print(f"{'Script':<15} {'Direct':>10} {'Via aio':>10} {'Overhead':>10} {'%':>8}")
    print("-" * 58)

    results = []
    for name, code in scripts:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False) as f:
            f.write(code)
            f.flush()
            path = f.name

        direct_ms, direct_std = time_cmd(f"time python3 {path} 2>&1")
        aio_ms, aio_std = time_cmd(f"time python3 {AIO} {path} 2>&1")
        overhead = aio_ms - direct_ms
        pct = (overhead / direct_ms * 100) if direct_ms > 0 else 0

        print(f"{name:<15} {direct_ms:>7.1f}ms {aio_ms:>7.1f}ms {overhead:>+7.1f}ms {pct:>7.1f}%")
        results.append((name, direct_ms, aio_ms, overhead, pct))
        os.unlink(path)

    print("-" * 58)
    avg_overhead = statistics.mean(r[3] for r in results)
    print(f"{'Average overhead:':<15} {avg_overhead:>+28.1f}ms")

    # Test aio commands directly
    print(f"\n{'aio command':<20} {'Time':>10}")
    print("-" * 32)
    for cmd in ["help", "ssh", "diff"]:
        ms, _ = time_cmd(f"time python3 {AIO} {cmd} 2>&1")
        print(f"aio {cmd:<16} {ms:>7.1f}ms")

if __name__ == '__main__':
    test_overhead()
