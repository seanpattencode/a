"""cloud_sync — tar.zst backup of a local dir to every configured a-gdrive remote.
Sole live export (imported by gdrive.py for `a gdrive sync`); `a sync` itself is C (net.c cmd_sync).
_merge_rclone seeds ~/.config/rclone/rclone.conf from the synced login/ copy so a fresh device can ship."""
import os, subprocess as sp
from pathlib import Path
from _common import SYNC_ROOT, RCLONE_BACKUP_PATH, DEVICE_ID, get_rclone, _configured_remotes

def _merge_rclone():
    import re
    lc, rc = SYNC_ROOT/'login'/'rclone.conf', Path.home()/'.config/rclone/rclone.conf'
    if not lc.exists(): return
    rc.parent.mkdir(parents=True, exist_ok=True)
    lt, rt = lc.read_text(), rc.read_text() if rc.exists() else ''
    for n in 'a-gdrive', 'a-gdrive2':
        if f'[{n}]' not in rt and (m := re.search(rf'\[{n}\][^\[]*', lt)):
            rc.write_text(rt + m.group() + '\n')
            rt = rc.read_text()

def cloud_sync(local_path, name):
    rc = get_rclone()
    _merge_rclone()
    if not rc: return False, "no rclone"
    tar = f'{os.getenv("TMPDIR", "/tmp")}/{name}-{DEVICE_ID}.tar.zst'
    if sp.run(f'tar -cf - -C {local_path} . 2>/dev/null | zstd -q > {tar}', shell=True).returncode > 1:
        return False, "tar failed"
    ok = [r for r in _configured_remotes() if sp.run([rc, 'copyto', tar, f'{r}:{RCLONE_BACKUP_PATH}/backup/{DEVICE_ID}/{name}.tar.zst', '-q']).returncode == 0]
    Path(tar).unlink(missing_ok=True)
    return bool(ok), f"{'✓'*len(ok) or 'x'} {','.join(ok) or 'fail'}"
