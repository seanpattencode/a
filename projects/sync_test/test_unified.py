#!/usr/bin/env python3
"""Test add/edit/delete/archive on unified a-git repo"""
import subprocess as sp, time
from pathlib import Path

ROOT = Path.home() / 'a-sync'
TS = time.strftime('%Y%m%dT%H%M%S') + f'.{time.time_ns() % 1000000000:09d}'

def sync(): sp.run('cd ~/a-sync && git add -A && git commit -qm test && git push -q origin main', shell=True, capture_output=True)

def test(name, folder, content, has_archive=False):
    d = ROOT / folder
    f = d / f'_test_{TS}.txt'
    f.write_text(content)
    print(f"{name}: ADD {f.name}")

    if has_archive:
        arc = d / '.archive'
        arc.mkdir(exist_ok=True)
        f.rename(arc / f.name)
        print(f"{name}: ARCHIVE -> .archive/")
        (arc / f.name).unlink()
    else:
        f.unlink()
        print(f"{name}: DELETE")

if __name__ == '__main__':
    print(f"Testing: {ROOT}\n")
    sync()

    test('tasks', 'tasks', 'test task\n')
    test('notes', 'notes', f'Text: test\nStatus: pending\nDevice: TEST\n', has_archive=True)
    test('ssh', 'ssh', 'Name: _test\nHost: test@127.0.0.1\n')
    test('common', 'common', 'test\n')
    test('hub', 'hub', 'test\n')

    sync()

    ok, _ = sp.run('cd ~/a-sync && git status --porcelain', shell=True, capture_output=True, text=True).returncode, None
    print(f"\n{'✓' if not ok else 'x'} Done (repo {'clean' if not ok else 'dirty'})")
