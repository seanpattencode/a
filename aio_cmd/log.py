"""aio log - View agent logs"""
import sys, os, time, subprocess as sp
from pathlib import Path
from datetime import datetime
from . _common import init_db, LOG_DIR, db_sync

def run():
    init_db()
    os.makedirs(LOG_DIR, exist_ok=True); db_sync(pull=True)
    logs = sorted(Path(LOG_DIR).glob('*.log'), key=lambda x: x.stat().st_mtime, reverse=True)
    total = sum(f.stat().st_size for f in logs); print(f"Logs: {len(logs)} files, {total/1024/1024:.1f}MB")
    if not logs: return
    wda = sys.argv[2] if len(sys.argv) > 2 else None
    if wda == 'clean': days = int(sys.argv[3]) if len(sys.argv) > 3 else 7; old = [f for f in logs if (time.time() - f.stat().st_mtime) > days*86400]; [f.unlink() for f in old]; print(f"✓ Deleted {len(old)} logs older than {days}d"); return
    if wda == 'tail': f = logs[int(sys.argv[3])] if len(sys.argv) > 3 and sys.argv[3].isdigit() else logs[0]; os.execvp('tail', ['tail', '-f', str(f)])
    print(f"  {'#':<3} {'date':<11} {'device':<10} {'session':<25} {'size':>6}")
    for i, f in enumerate(logs[:20]):
        sz, nm, mt = f.stat().st_size/1024, f.stem, f.stat().st_mtime; parts = nm.split('__'); dev, sn = (parts[0][:10], '__'.join(parts[1:])) if len(parts) > 1 else ('-', nm)
        print(f"  {i}. {datetime.fromtimestamp(mt).strftime('%m/%d %H:%M')}  {dev:<10} {sn:<25} {sz:>5.0f}KB")
    print(f"\naio log tail [#] | aio log clean [days]")
    if (c := input("> ").strip()).isdigit() and int(c) < len(logs): sp.run(['tmux', 'new-window', f'cat "{logs[int(c)]}"; read'])
