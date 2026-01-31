"""a task - add and list tasks"""
import sys; from ._common import SYNC_ROOT; from .sync import sync
def run():
    f, a = SYNC_ROOT/'common'/'tasks.txt', sys.argv[2:]; sync('common'); t = f.read_text().strip().split('\n') if f.exists() and f.read_text().strip() else []
    if not a: [print(f"{i}. {x}") for i,x in enumerate(t,1)]; print("\n0. ask AI what should be #1") if t else None
    elif a[0]=='0': import subprocess; print("Analyzing...",flush=True); subprocess.run(['a','x.priority'])
    elif a[0].isdigit() and len(a)>1 and a[1]=='top': t.insert(0,t.pop(int(a[0])-1)); f.write_text('\n'.join(t)+'\n'); sync('common')
    else: f.write_text('\n'.join(t+[' '.join(a)])+'\n'); sync('common')
