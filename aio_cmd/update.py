"""aio update - Update aio"""
import os, subprocess as sp
from . _common import _sg, list_all, SCRIPT_DIR

def run():
    if _sg('rev-parse', '--git-dir').returncode != 0: print("x Not in git repo"); return
    print("Checking..."); before = _sg('rev-parse', 'HEAD').stdout.strip()[:8]
    if not before or _sg('fetch').returncode != 0: return
    if 'behind' in _sg('status', '-uno').stdout: print("Downloading..."); _sg('pull', '--ff-only'); print(f"✓ {before} -> {_sg('rev-parse', 'HEAD').stdout.strip()[:8]}")
    else: print(f"✓ Up to date ({before})")
    i = os.path.join(SCRIPT_DIR, 'install.sh'); os.path.exists(i) and sp.run(['bash', i]); list_all()
