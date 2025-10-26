#!/usr/bin/env python3
import os, sys

cmd_to_run = sys.argv[1:] if len(sys.argv) > 1 else ['top']
base_cmd = ['tmux', 'new-window'] if "TMUX" in os.environ else ['tmux', 'new', '-A', '-s', 'main']
try:
    os.execvp('tmux', base_cmd + cmd_to_run)
except (FileNotFoundError, OSError) as e:
    print(f"tmux error: {e}", file=sys.stderr)
    sys.exit(1)
