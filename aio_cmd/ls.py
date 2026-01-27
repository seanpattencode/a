"""aio ls [#] - List tmux sessions, optionally attach by number"""
import sys, os, subprocess as sp

def _attach(s): os.execvp('tmux', ['tmux', 'switch-client' if 'TMUX' in os.environ else 'attach', '-t', s])

def run():
    r = sp.run(['tmux', 'list-sessions', '-F', '#{session_name}'], capture_output=True, text=True)
    if r.returncode != 0: print("No tmux sessions"); sys.exit(0)
    sl = [s for s in r.stdout.strip().split('\n') if s]
    if not sl: print("No tmux sessions"); sys.exit(0)

    sel = sys.argv[2] if len(sys.argv) > 2 else None
    if sel and sel.isdigit() and (i := int(sel)) < len(sl): _attach(sl[i])

    for i, s in enumerate(sl):
        pr = sp.run(['tmux', 'display-message', '-p', '-t', s, '#{pane_current_path}'], capture_output=True, text=True)
        print(f"  {i}  {s}: {pr.stdout.strip() if pr.returncode == 0 else ''}")
