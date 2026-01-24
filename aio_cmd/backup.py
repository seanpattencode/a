"""aio backup - Backup sync status"""
import sys, os, subprocess as sp, shutil
from . _common import init_db, DATA_DIR, _die

def run():
    init_db()
    wda = sys.argv[2] if len(sys.argv) > 2 else None
    if wda == 'setup':
        url = sys.argv[3] if len(sys.argv) > 3 else (sp.run(['gh', 'repo', 'view', 'aio-sync', '--json', 'url', '-q', '.url'], capture_output=True, text=True).stdout.strip() or sp.run(['gh', 'repo', 'create', 'aio-sync', '--private', '-y'], capture_output=True, text=True).stdout.strip()) if shutil.which('gh') else None
        if not url: _die("x No URL (need gh CLI or provide URL)")
        sp.run(f'cd "{DATA_DIR}" && git init -q 2>/dev/null; git remote set-url origin {url} 2>/dev/null || git remote add origin {url}; git fetch origin 2>/dev/null && git reset --hard origin/main 2>/dev/null || (git add -A && git commit -m "init" -q && git push -u origin main)', shell=True); print("✓ Sync ready"); return
    gu = sp.run(f'cd "{DATA_DIR}" && git remote get-url origin 2>/dev/null', shell=True, capture_output=True, text=True).stdout.strip()
    print(f"Git: {'✓ '+gu if gu else 'x (aio backup setup)'}")
