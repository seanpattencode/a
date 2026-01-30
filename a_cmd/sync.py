"""aio sync - Git-based sync to GitHub
RFC 5322 with .txt is the default and preferred way to format data, because it doesn't hide information, doesn't break, and yet is machine searchable with metadata.
This allows for isolation, so a sync conflict in notes doesn't bottleneck agent work logging."""
import os, subprocess as sp
from pathlib import Path
from ._common import SCRIPT_DIR

SYNC_ROOT = Path(SCRIPT_DIR).parent / 'a-sync'
REPOS = {'common': 'aio-common', 'ssh': 'aio-ssh', 'logs': 'aio-logs', 'login': 'aio-login'}

def _sync_repo(path, repo_name, msg='sync'):
    path.mkdir(parents=True, exist_ok=True)
    r = sp.run(f'cd {path}; git init -q; git add -A; git commit -qm "{msg}" 2>/dev/null; gh repo create {repo_name} --private 2>/dev/null; URL=$(gh api user -q \'"https://github.com/"+.login+"/{repo_name}.git"\'); git remote set-url origin $URL 2>/dev/null || git remote add origin $URL; git pull --rebase -q origin HEAD 2>/dev/null; git push -u origin HEAD -q', shell=True, capture_output=True, text=True)
    url = sp.run(['git','-C',str(path),'remote','get-url','origin'], capture_output=True, text=True).stdout.strip() or 'syncing...'
    if r.returncode: print(f"x sync failed [{repo_name}] - copy this to ai agent (hint: a copy):\n  Sync conflict in {path}. Verify: {url}\n  Run `cd {path} && git status`, explain the issue, propose a fix plan for my approval.")
    return url

def sync(repo='common', msg='sync'):
    return _sync_repo(SYNC_ROOT/repo, REPOS.get(repo, f'aio-{repo}'), msg)

def sync_all(msg='sync'):
    return {r: _sync_repo(SYNC_ROOT/r, name, msg) for r, name in REPOS.items()}

def run():
    print(f"{SYNC_ROOT}")
    for repo, name in REPOS.items():
        path = SYNC_ROOT/repo; url = _sync_repo(path, name)
        t = sp.run(['git','-C',str(path),'log','-1','--format=%cd %s','--date=format:%Y-%m-%d %I:%M:%S %p'],capture_output=True,text=True).stdout.strip()
        files = [f for f in sp.run(['git','-C',str(path),'ls-files'],capture_output=True,text=True).stdout.split() if f]
        print(f"\n[{repo}] {url}\nLast: {t}"); [print(f"  {f}") for f in files]
