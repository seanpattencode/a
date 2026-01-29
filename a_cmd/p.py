"""aio p - List projects or send prompt to agent"""
import sys
from . _common import init_db, list_all, send_to_sess, tm, load_cfg, get_prefix

def run():
    init_db()
    if len(sys.argv) <= 2: list_all(); return
    r = tm.ls()
    if r.returncode != 0: print("x No tmux"); return
    sess = [s for s in r.stdout.strip().split('\n') if any(a in s.lower() for a in ['claude', 'codex', 'gemini', 'aider'])]
    if not sess: print("x No agent session"); return
    sn, prompt, cfg = sess[0], ' '.join(sys.argv[2:]), load_cfg()
    pre = get_prefix(next((a for a in ['claude', 'codex', 'gemini'] if a in sn.lower()), 'claude'), cfg)
    send_to_sess(sn, (pre + prompt) if pre else prompt, enter=len(sys.argv) == 3)
