#!/usr/bin/env python3
"""a reboot [now] — snapshot, show what respawns, reboot; now = skip confirm. Boot restore fires once per tmux server (lib/tmux.c @res)."""
import os, sys, res

if res.save():
    if "now" in sys.argv or (print("\nreboot now? [y/N]: ", end="", flush=True) or sys.stdin.readline().strip().lower() == "y"):
        os.execvp("sh", ["sh", "-c", "systemctl reboot || sudo reboot"])
print("x not rebooting (snapshot kept)")
