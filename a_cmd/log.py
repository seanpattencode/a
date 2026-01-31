"""aio log [#|tail #|clean days|grab|sync] - View agent logs"""
import sys, os, time, subprocess as sp, shutil
from pathlib import Path
from datetime import datetime
from ._common import init_db, LOG_DIR, DEVICE_ID, RCLONE_REMOTES, RCLONE_BACKUP_PATH, get_rclone
from .sync import cloud_sync

CLAUDE_DIR = Path.home()/'.claude'

def _grab():
    """Copy Claude logs to local log dir and sync"""
    dst = Path(LOG_DIR)/'claude'; dst.mkdir(parents=True, exist_ok=True)
    n = 0
    if (h := CLAUDE_DIR/'history.jsonl').exists(): shutil.copy2(h, dst/'history.jsonl'); n += 1
    for f in CLAUDE_DIR.glob('projects/**/*.jsonl'):
        rel = f.relative_to(CLAUDE_DIR/'projects'); (dst/'projects'/rel.parent).mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dst/'projects'/rel); n += 1
    ok, msg = cloud_sync(LOG_DIR, 'logs')
    print(f"{'✓' if ok else 'x'} {n} files {msg}")

def run():
    init_db(); Path(LOG_DIR).mkdir(parents=True, exist_ok=True)
    sel = sys.argv[2] if len(sys.argv) > 2 else None

    if sel == 'grab': _grab(); return
    if sel == 'sync': ok, msg = cloud_sync(LOG_DIR, 'logs'); print(f"{'✓' if ok else 'x'} {msg}"); return

    logs = sorted(Path(LOG_DIR).glob('*.log'), key=lambda x: x.stat().st_mtime, reverse=True)
    if sel == 'clean': days = int(sys.argv[3]) if len(sys.argv) > 3 else 7; old = [f for f in logs if (time.time() - f.stat().st_mtime) > days*86400]; [f.unlink() for f in old]; print(f"✓ {len(old)} logs"); return
    if sel == 'tail': f = logs[int(sys.argv[3])] if len(sys.argv) > 3 and sys.argv[3].isdigit() else (logs[0] if logs else None); f and os.execvp('tail', ['tail', '-f', str(f)]); return
    if sel and sel.isdigit() and logs and (i := int(sel)) < len(logs): sp.run(['tmux', 'new-window', f'cat "{logs[i]}"; read']); return

    # Status
    total = sum(f.stat().st_size for f in logs) if logs else 0
    print(f"Logs: {len(logs)}, {total/1024/1024:.1f}MB local")
    rc = get_rclone()
    if rc:
        for r in RCLONE_REMOTES:
            path = f'{r}:{RCLONE_BACKUP_PATH}/logs'
            try:
                res = sp.run([rc,'lsjson',path,'--dirs-only'], capture_output=True, text=True, timeout=15)
                if res.returncode == 0:
                    import json; dirs = {d['Name']:d['ID'] for d in json.loads(res.stdout)}
                    fid = dirs.get(DEVICE_ID,'')
                    url = f"https://drive.google.com/drive/folders/{fid}" if fid else "x"
                    print(f"  {r}: {url}")
                else: print(f"  {r}: x")
            except: print(f"  {r}: timeout")

    for name, base, files in [('Claude', CLAUDE_DIR, ['history.jsonl','projects']), ('Codex', Path.home()/'.codex', ['history.jsonl','sessions'])]:
        if base.exists(): parts = [f"{f}({(base/f).stat().st_size//1024}KB)" if (base/f).is_file() else f"{f}({len(list((base/f).iterdir()))})" for f in files if (base/f).exists()]; parts and print(f"  {name}: [{', '.join(parts)}]")

    if logs:
        for i, f in enumerate(logs[:15]):
            sz, nm, mt = f.stat().st_size/1024, f.stem, f.stat().st_mtime
            sn = '__'.join(nm.split('__')[1:]) if '__' in nm else nm
            print(f"{i:>2} {datetime.fromtimestamp(mt):%m/%d %H:%M} {sn[:28]:<28} {sz:>5.0f}K")
        print("\na log 0     view\na log sync  sync to cloud")
    else: print("No logs yet")
