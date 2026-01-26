"""aio push - instant background commit+push"""
import sys, os, subprocess as sp

_LOG = os.path.expanduser('~/.local/share/aios/logs/push.log')

def _check():
    if os.path.exists(_LOG) and (f := open(_LOG).read().strip()) and 'FAIL' in f and 'up-to-date' not in f: print(f"! {f}")
    os.path.exists(_LOG) and open(_LOG, 'w').close()

def run():
    _check()
    cwd, msg = os.getcwd(), ' '.join(sys.argv[2:]) or f"Update {os.path.basename(os.getcwd())}"
    os.makedirs(os.path.dirname(_LOG), exist_ok=True)
    s = f'cd "{cwd}" && git add -A && git commit -m "{msg}" --allow-empty; r=$(git push 2>&1); echo "$r" | grep -q "up-to-date\\|OK\\|done\\|->\\|Everything" && echo "[OK]" > "{_LOG}" || echo "[FAIL] $r" > "{_LOG}"'
    sp.Popen(['sh', '-c', s], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
    print(f"✓ {msg}")
