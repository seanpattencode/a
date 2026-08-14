#!/usr/bin/env python3
"""a reboot [now] — snapshot, show what respawns, reboot. now = skip confirm (wrappers: i reboot adds guard/notify/abort policy). Boot restore fires once per tmux server — lib/tmux.c @res."""
import os, sys, res

if res.save():                                        # prints each window + its resume cmd = what respawns
    if "now" in sys.argv or (print("\nreboot now? [y/N]: ", end="", flush=True) or sys.stdin.readline().strip().lower() == "y"):
        os.execvp("sh", ["sh", "-c", "systemctl reboot || sudo reboot"])
print("x not rebooting (snapshot kept)")
