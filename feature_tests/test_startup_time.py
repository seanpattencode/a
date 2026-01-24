#!/usr/bin/env python3
"""Test startup time of aio commands"""
import subprocess as sp, re

TESTS = [
    ("aio", "bash cats cache"),
    ("python3 ~/.local/bin/aio", "python direct"),
    ("aio i", "cache + python"),
    ("python3 ~/.local/bin/aio i", "python direct"),
    ("aio-i", "cache + python"),
]

def get_time(cmd):
    r = sp.run(['bash', '-i', '-c', f'time {cmd} 2>&1 | head -1'], stdin=sp.DEVNULL, capture_output=True, text=True)
    m = re.search(r'real\s+0m([\d.]+)s', r.stderr)
    out = re.sub(r'bash:.*\n', '', r.stdout).strip().split('\n')[0][:30]
    return float(m.group(1)) * 1000 if m else 0, out

if __name__ == '__main__':
    print(f"{'Command':<28} {'Time':>7}  {'Output':<25} {'Why'}")
    print("-" * 80)
    for cmd, desc in TESTS:
        ms, out = get_time(cmd)
        print(f"{cmd:<28} {ms:>6.0f}ms  {out:<25} {desc}")
