"""aio sync - Git-based sync to GitHub
RFC 5322 with .txt is the default and preferred way to format data, because it doesn't hide information, doesn't break, and yet is machine searchable with metadata."""
import os, subprocess as sp
DATA = os.path.expanduser('~/.local/share/a')

def sync(msg='sync', bg=True):
    os.makedirs(DATA, exist_ok=True); cmd='cd {}; git init -q; git add -A; git commit -qm "{}" 2>/dev/null; git remote get-url origin || gh repo create aio-sync --private --source . --push -y; git pull --rebase -q 2>/dev/null; git push -q'.format(DATA, msg)
    (sp.Popen if bg else sp.run)(cmd, shell=True, stdout=sp.DEVNULL, stderr=sp.DEVNULL); return sp.run(['git','-C',DATA,'remote','get-url','origin'], capture_output=True, text=True).stdout.strip() or 'syncing...'

def run():
    print(f"{DATA}\n{sync(bg=False)}"); r=sp.run(['git','-C',DATA,'log','origin/main','-1','--format=%cd %s','--date=format:%Y-%m-%d %I:%M:%S %p'],capture_output=True,text=True).stdout.strip(); print(f"Last: {r}")
    [print(f) for f in sp.run(['git','-C',DATA,'ls-files'],capture_output=True,text=True).stdout.split() if f]
