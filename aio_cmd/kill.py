"""aio kill - Kill all tmux sessions"""
import subprocess as sp

def run():
    if input("Kill all tmux sessions? (y/n): ").lower() in ['y', 'yes']:
        print("✓ Killed all tmux")
        sp.run(['tmux', 'kill-server'])
