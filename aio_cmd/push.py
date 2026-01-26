"""aio push - instant background commit+push"""
import sys, os, subprocess as sp

_LOG = os.path.expanduser('~/.local/share/aios/logs/push.log')

def _check():
    if os.path.exists(_LOG) and '[FAIL]' in (f := open(_LOG).read()): print(f"! {f.strip()}")
    os.path.exists(_LOG) and open(_LOG, 'w').close()

def run():
    _check()
    cwd, msg = os.getcwd(), ' '.join(sys.argv[2:]) or f"Update {os.path.basename(os.getcwd())}"
    os.makedirs(os.path.dirname(_LOG), exist_ok=True)
    s = f'cd "{cwd}" && git add -A && git commit -m "{msg}" --allow-empty-message && git push 2>&1 && echo "[OK]" > "{_LOG}" || echo "[FAIL] $(git push 2>&1 | head -1)" > "{_LOG}"'
    sp.Popen(['sh', '-c', s], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
    print("✓ Pushing...")
