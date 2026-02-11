# Append-only sync: files get timestamps, git only sees additions, no conflicts
# Unified repo: ~/a-sync/ -> github.com/seanpattencode/a-git
"""a sync - Append-only sync to GitHub (no conflicts possible)"""
import os, subprocess as sp, time, shlex, signal
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from ._common import SYNC_ROOT, RCLONE_REMOTES, RCLONE_BACKUP_PATH, DEVICE_ID, get_rclone

FOLDERS = 'common ssh login hub notes workspace docs tasks'.split()
POLL_INTERVAL = 60

def q(p): return shlex.quote(str(p))
def ts(): return time.strftime('%Y%m%dT%H%M%S') + f'.{time.time_ns() % 1000000000:09d}'
def get_latest(path, name): return (m := sorted(Path(path).glob(f'{name}_*.txt'))) and m[-1] or None

def add_timestamps(path, recursive=False):
    """Add timestamps to files missing them"""
    t = ts()
    for p in Path(path).glob('**/*.txt' if recursive else '*.txt'):
        if '_20' not in p.stem and not p.name.startswith('.'):
            p.rename(p.with_name(f'{p.stem}_{t}{p.suffix}'))

def _broadcast():
    """Background: ping all SSH hosts to pull"""
    if os.fork() > 0: return
    os.setsid()
    hosts = []
    for f in (SYNC_ROOT / 'ssh').glob('*.txt'):
        d = {k.strip(): v.strip() for l in f.read_text().splitlines() if ':' in l for k, v in [l.split(':', 1)]}
        if d.get('Host') and d.get('Name') != DEVICE_ID:
            hosts.append((d['Host'], d.get('Password')))
    def ping(hp):
        try:
            h, pw = hp; p = h.rsplit(':', 1)
            cmd = (['sshpass', '-p', pw] if pw else []) + ['ssh', '-oConnectTimeout=2', '-oStrictHostKeyChecking=no'] + (['-p', p[1]] if len(p) > 1 else []) + [p[0], 'cd ~/projects/a-sync 2>/dev/null && git pull -q origin main || cd ~/a-sync && git pull -q origin main']
            sp.run(cmd, capture_output=True, timeout=5)
        except: pass
    for _ in range(3):
        with ThreadPoolExecutor(max_workers=len(hosts) or 1) as ex: list(ex.map(ping, hosts))
        time.sleep(3)
    os._exit(0)

def _sync(path=None, silent=False):
    """Sync: pull, commit local, push. Returns (success, conflict)"""
    p = q(path or SYNC_ROOT)
    pull = sp.run(f'cd {p} && git pull -q --no-rebase origin main', shell=True, capture_output=True, text=True)
    if pull.returncode != 0 and any(x in (pull.stderr + pull.stdout).lower() for x in ('conflict', 'diverged')):
        if not silent: print(f"! Sync conflict - run: cd {path} && git add -A && git commit -m fix && git push --force")
        return False, True
    sp.run(f'cd {p} && git add -A', shell=True, capture_output=True)
    if sp.run(f'cd {p} && git commit -qm sync', shell=True, capture_output=True).returncode == 0:
        push = sp.run(f'cd {p} && git push -q origin main', shell=True, capture_output=True, text=True)
        if push.returncode != 0:
            if 'rejected' in (push.stderr + push.stdout).lower():
                if not silent: print("! Push rejected - try: a sync")
                return False, True
            return False, False
        _broadcast()
    return True, False

def _merge_rclone():
    import re
    lc, rc = SYNC_ROOT/'login'/'rclone.conf', Path.home()/'.config/rclone/rclone.conf'
    if not lc.exists(): return
    rc.parent.mkdir(parents=True, exist_ok=True)
    lt, rt = lc.read_text(), rc.read_text() if rc.exists() else ''
    for n in ('a-gdrive', 'a-gdrive2'):
        if f'[{n}]' not in rt and (m := re.search(rf'\[{n}\][^\[]*', lt)):
            rc.write_text(rt + m.group() + '\n'); rt = rc.read_text()

def cloud_sync(local_path, name):
    rc = get_rclone(); _merge_rclone()
    if not rc: return False, "no rclone"
    tar = f'{os.getenv("TMPDIR", "/tmp")}/{name}-{DEVICE_ID}.tar.zst'
    if sp.run(f'tar -cf - -C {local_path} . 2>/dev/null | zstd -q > {tar}', shell=True).returncode > 1:
        return False, "tar failed"
    ok = [r for r in RCLONE_REMOTES if sp.run([rc, 'copyto', tar, f'{r}:{RCLONE_BACKUP_PATH}/{name}/{DEVICE_ID}.tar.zst', '-q']).returncode == 0]
    Path(tar).unlink(missing_ok=True)
    return bool(ok), f"{'+'*len(ok) or 'x'} {','.join(ok) or 'fail'}"

def sync(folder=None): return _sync(SYNC_ROOT)

def sync_file(path, content=None):
    """Write new version of file and sync. Returns path to latest."""
    path = Path(path)
    name = path.stem.rsplit('_', 1)[0] if '_20' in path.stem else path.stem
    if content is not None:
        (path.parent / f'{name}_{ts()}{path.suffix}').write_text(content)
    _sync(SYNC_ROOT, silent=True)
    return get_latest(path.parent, name)

def _init_repo():
    if (SYNC_ROOT / '.git').exists(): return True
    return sp.run(f'gh repo clone seanpattencode/a-git {q(SYNC_ROOT)} || (cd {q(SYNC_ROOT)} && git init -q -b main && echo "backup/\\nlogs/\\n.archive/" > .gitignore && git add -A && git commit -qm init && gh repo create a-git --private --source=. --push)', shell=True, capture_output=True).returncode == 0

def _pid_file(): return Path.home() / '.a-sync-poll.pid'

def start_poll_daemon():
    pf = _pid_file()
    if pf.exists():
        try: os.kill(int(pf.read_text().strip()), 0); print(f"Poll daemon already running"); return
        except OSError: pass
    if (pid := os.fork()) > 0: pf.write_text(str(pid)); print(f"Poll daemon started (pid {pid}, {POLL_INTERVAL}s)"); return
    os.setsid()
    while True: time.sleep(POLL_INTERVAL); sp.run(f'cd {q(SYNC_ROOT)} && git pull -q origin main', shell=True, capture_output=True)

def stop_poll_daemon():
    pf = _pid_file()
    if not pf.exists(): print("Poll daemon not running"); return
    try: os.kill(int(pf.read_text().strip()), signal.SIGTERM); pf.unlink(); print("Poll daemon stopped")
    except OSError: pf.unlink(); print("Poll daemon was not running")

HELP = """a sync - Append-only sync (no conflicts)
  a sync        Sync all data
  a sync all    Sync + broadcast to SSH hosts
  a sync poll   Start poll daemon (60s)
  a sync stop   Stop poll daemon"""

def run():
    import sys
    args = sys.argv[2:]
    if args and args[0] in ('help', '-h', '--help'): print(HELP); return
    if args and args[0] == 'poll': start_poll_daemon(); return
    if args and args[0] == 'stop': stop_poll_daemon(); return
    _init_repo()
    ok, conflict = _sync(SYNC_ROOT)
    url = sp.run(['git', '-C', str(SYNC_ROOT), 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip()
    t = sp.run(['git', '-C', str(SYNC_ROOT), 'log', '-1', '--format=%cd %s', '--date=format:%Y-%m-%d %I:%M:%S %p'], capture_output=True, text=True).stdout.strip()
    print(f"{SYNC_ROOT}\n  {url}\n  Last: {t}\n  Status: {'CONFLICT' if conflict else 'synced' if ok else 'no changes'}")
    for f in FOLDERS:
        if (p := SYNC_ROOT / f).exists(): print(f"  {f}: {len(list(p.glob('*.txt')))} files")
    pf = _pid_file()
    running = False
    if pf.exists():
        try: os.kill(int(pf.read_text().strip()), 0); running = True
        except: pass
    print(f"\n  Poll: {'running' if running else 'not running'} - {'stop' if running else 'start'} with: a sync {'stop' if running else 'poll'}")
    if args and args[0] == 'all': print("\n--- Broadcasting ---"); sp.run('a ssh all "a sync"', shell=True)
