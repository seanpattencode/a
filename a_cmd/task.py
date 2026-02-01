# Append-only task system: one file per task, edits create new files, no conflicts
import sys;from ._common import SYNC_ROOT;from .sync import sync
def run():
    d,a=SYNC_ROOT/'common'/'tasks',sys.argv[2:];d.mkdir(exist_ok=True);sync('common');t=sorted(d.glob('*.txt'))
    if not a:[print(f"{i}. {f.read_text().strip()}")for i,f in enumerate(t,1)]
    elif a[0]=='0':import subprocess;print("Analyzing...",flush=True);subprocess.run(['a','x.priority'])
    else:(d/f'{len(t)+1:03d}.txt').write_text(' '.join(a)+'\n');sync('common')
