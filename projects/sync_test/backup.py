import subprocess as sp, time
from pathlib import Path

ROOT = Path(__file__).parent

def backup(): ts=time.strftime('%Y%m%dT%H%M%S'); sp.run(f'tar -czf {ROOT}/sync_test_{ts}.tar.gz -C {ROOT} devices test_sync.py && cp {ROOT}/sync_test_{ts}.tar.gz /data/data/com.termux/files/home/a/projects/', shell=True); return f'sync_test_{ts}.tar.gz'

if __name__ == '__main__': print(backup())
