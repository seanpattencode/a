"""aio sync - Git-based sync to GitHub"""
import os, subprocess as sp
DATA = os.path.expanduser('~/.local/share/a')

def sync(msg='sync', bg=True):
    os.makedirs(DATA, exist_ok=True); cmd='cd {}; git init -q; git add -A; git commit -qm "{}" --allow-empty; git remote get-url origin || gh repo create aio-sync --private --source . --push -y; git push -q'.format(DATA, msg)
    (sp.Popen if bg else sp.run)(cmd, shell=True, stdout=sp.DEVNULL, stderr=sp.DEVNULL); return sp.run(['git','-C',DATA,'remote','get-url','origin'], capture_output=True, text=True).stdout.strip() or 'syncing...'

def run():
    print(f"{DATA}\n{sync(bg=False)}"); t=sp.run(['git','-C',DATA,'log','-1','--format=%cd','--date=format:%Y-%m-%d %I:%M:%S %p'],capture_output=True,text=True).stdout.strip(); print(f"Last sync: {t}")
    [print(f) for f in sp.run(['git','-C',DATA,'ls-files'],capture_output=True,text=True).stdout.split() if f]
