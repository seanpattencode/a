"""aio update - Update aio"""
from . _common import _sg, list_all

def run():
    if _sg('rev-parse', '--git-dir').returncode != 0: print("x Not in git repo"); return
    print("Checking..."); before = _sg('rev-parse', 'HEAD').stdout.strip()[:8]
    if not before or _sg('fetch').returncode != 0: return
    if 'behind' not in _sg('status', '-uno').stdout: print(f"✓ Up to date ({before})"); list_all(); return
    print("Downloading..."); _sg('pull', '--ff-only')
    after = _sg('rev-parse', 'HEAD'); print(f"✓ {before} -> {after.stdout.strip()[:8]}" if after.returncode == 0 else "✓ Done")
    list_all(); print("Run: source ~/.bashrc")
