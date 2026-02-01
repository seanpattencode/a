# Append-only: {time_ns}_{device}.txt = no conflicts. Push ours, reset to main.
import sys,time;from ._common import SYNC_ROOT,DEVICE_ID as D;from .sync import sync
def run():
    d,a=SYNC_ROOT/'common'/'tasks',sys.argv[2:];d.mkdir(exist_ok=True);sync('common');t=sorted(d.glob('*.txt'))
    if not a:[print(f"{i}. {f.read_text().strip()}")for i,f in enumerate(t,1)]
    elif a[0]=='0':import subprocess;print("Analyzing...",flush=True);subprocess.run(['a','x.priority'])
    else:(d/f'{time.time_ns()}_{D}.txt').write_text(' '.join(a)+'\n');sync('common')
