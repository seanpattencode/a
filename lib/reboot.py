#!/usr/bin/env python3
"""a reboot — fresh snapshot, show exactly which tmux windows respawn on next boot, confirm, then reboot.
(respawn = `a snap restore`, fired automatically on first session-create after boot — tm_ensure_sess, lib/tmux.c)"""
import os, sys, res

if res.save():                                        # prints each window + its resume cmd = what respawns
    print("\nreboot now? [y/N]: ", end="", flush=True)
    if sys.stdin.readline().strip().lower() == "y":
        os.execvp("sh", ["sh", "-c", "systemctl reboot || sudo reboot"])
print("x not rebooting (snapshot kept)")
