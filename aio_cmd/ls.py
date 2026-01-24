"""aio ls - List tmux sessions"""
import sys, subprocess as sp

def run():
    r = sp.run(['tmux', 'list-sessions', '-F', '#{session_name}'], capture_output=True, text=True)
    if r.returncode != 0: print("No tmux sessions found"); sys.exit(0)
    sl = [s for s in r.stdout.strip().split('\n') if s]
    if not sl: print("No tmux sessions found"); sys.exit(0)
    print("Tmux Sessions:\n")
    for s in sl:
        pr = sp.run(['tmux', 'display-message', '-p', '-t', s, '#{pane_current_path}'], capture_output=True, text=True)
        print(f"  {s}: {pr.stdout.strip() if pr.returncode == 0 else ''}")
