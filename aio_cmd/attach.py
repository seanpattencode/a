"""aio attach - Reconnect to session"""
import os, subprocess as sp
from . _common import init_db, load_cfg, db, tm

def run():
    init_db()
    cfg = load_cfg()
    WT_DIR = cfg.get('worktrees_dir', os.path.expanduser("~/projects/aiosWorktrees"))
    cwd = os.getcwd()

    def _a(s): os.execvp('tmux', ['tmux', 'switch-client' if 'TMUX' in os.environ else 'attach', '-t', s])

    if WT_DIR in cwd:
        p = cwd.replace(WT_DIR + '/', '').split('/')
        if len(p) >= 2 and tm.has(s := f"{p[0]}-{p[1]}"): _a(s)

    with db() as c: runs = c.execute("SELECT id, repo FROM multi_runs ORDER BY created_at DESC LIMIT 10").fetchall()
    if runs:
        for i, (rid, repo) in enumerate(runs): print(f"{i}. {'●' if tm.has(f'{os.path.basename(repo)}-{rid}') else '○'} {os.path.basename(repo)}-{rid}")
        ch = input("Select #: ").strip()
        if ch.isdigit() and int(ch) < len(runs): _a(f"{os.path.basename(runs[int(ch)][1])}-{runs[int(ch)][0]}")
    print("No session")
