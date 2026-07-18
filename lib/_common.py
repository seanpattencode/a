"""Shared utilities for lib/*.py commands"""
import os, subprocess as sp, shutil
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
ADATA_ROOT = next((p for p in [Path(SCRIPT_DIR)/'adata', Path.home()/'a'/'adata', Path.home()/'adata'] if (p/'git').exists()), Path(SCRIPT_DIR)/'adata')
DATA_DIR, SYNC_ROOT = str(ADATA_ROOT/'local'), ADATA_ROOT/'git'
RCLONE_REMOTE_PREFIX, RCLONE_BACKUP_PATH = 'a-gdrive', 'adata'
ACTIVITY_DIR = ADATA_ROOT/'git'/'activity'
def _get_dev():
    f = os.path.join(DATA_DIR, '.device')
    if os.path.exists(f): return open(f).read().strip()
    import socket; d = (sp.run(['getprop','ro.product.model'],capture_output=True,text=True).stdout.strip().replace(' ','-') or socket.gethostname()) if os.path.exists('/data/data/com.termux') else socket.gethostname()
    os.makedirs(os.path.dirname(f), exist_ok=True); open(f,'w').write(d); return d
DEVICE_ID = _get_dev()

def alog(msg):
    ACTIVITY_DIR.mkdir(parents=True, exist_ok=True); now = datetime.now(); cwd = os.getcwd()
    r = sp.run(['git','remote','get-url','origin'], capture_output=True, text=True, cwd=cwd) if os.path.isdir(cwd+'/.git') else None
    repo = f' git:{r.stdout.strip()}' if r and r.returncode == 0 and r.stdout.strip() else ''
    (ACTIVITY_DIR/now.strftime(f'%Y%m%dT%H%M%S.{int(now.timestamp()*1000)%1000:03d}_{DEVICE_ID}.txt')).write_text(f'{now:%m/%d %H:%M} {DEVICE_ID} {msg} {cwd}{repo}\n')

# Cloud
def get_rclone(): return shutil.which('rclone') or next((p for p in ['/usr/bin/rclone', os.path.expanduser('~/.local/bin/rclone')] if os.path.isfile(p)), None)
def _configured_remotes():
    if not (rc := get_rclone()): return []
    r = sp.run([rc, 'listremotes'], capture_output=True, text=True)
    return [l.rstrip(':') for l in r.stdout.splitlines() if l.rstrip(':').startswith(RCLONE_REMOTE_PREFIX)] if r.returncode == 0 else []
def cloud_sync(wait=False):
    rc, remotes = get_rclone(), _configured_remotes()
    if not rc or not remotes: return False, None
    def _sync():
        ok = True
        for rem in remotes:
            r = sp.run([rc, 'copy', DATA_DIR, f'{rem}:{RCLONE_BACKUP_PATH}/backup/data', '-q', '--exclude', '*.db*', '--exclude', '*cache*', '--exclude', 'timing.jsonl', '--exclude', '.device', '--exclude', '.git/**', '--exclude', 'logs/**'], capture_output=True, text=True)
            for f in ['~/.config/gh/hosts.yml', '~/.config/rclone/rclone.conf']:
                p = os.path.expanduser(f); os.path.exists(p) and sp.run([rc, 'copy', p, f'{rem}:{RCLONE_BACKUP_PATH}/backup/auth/', '-q'], capture_output=True)
            ok = ok and r.returncode == 0
        Path(DATA_DIR, '.gdrive_sync').touch() if ok else None; return ok
    return (True, _sync()) if wait else (__import__('threading').Thread(target=_sync, daemon=True).start(), (True, None))[1]
def _cloud_install():
    u=os.uname();s,bd,arch='osx'if u.sysname=='Darwin'else'linux',os.path.expanduser('~/.local/bin'),'amd64'if u.machine in('x86_64','AMD64')else'arm64'
    if sp.run(f'curl -sL https://downloads.rclone.org/rclone-current-{s}-{arch}.zip -o /tmp/rclone.zip && unzip -qjo /tmp/rclone.zip "*/rclone" -d {bd} && chmod +x {bd}/rclone', shell=True).returncode == 0:
        return f'{bd}/rclone'
    return None
def cloud_login(remote=None, custom=False):
    rc = get_rclone() or _cloud_install()
    if not rc: print("x rclone install failed"); return False
    existing = _configured_remotes()
    rem = remote or (RCLONE_REMOTE_PREFIX if RCLONE_REMOTE_PREFIX not in existing else f'{RCLONE_REMOTE_PREFIX}{len(existing)+1}')
    cmd = [rc, 'config', 'create', rem, 'drive']
    if custom:
        cid = input("client_id: ").strip(); csec = input("client_secret: ").strip()
        if not cid or not csec: print("x Both required"); return False
        cmd += ['client_id', cid, 'client_secret', csec]
    sp.run(cmd)
    if rem not in _configured_remotes(): print("x Login failed"); return False
    cloud_sync(wait=True); return True
def cloud_logout(remote=None):
    remotes = _configured_remotes()
    if not remotes: print("Not logged in"); return False
    sp.run([get_rclone(), 'config', 'delete', remote or remotes[-1]]); return True
