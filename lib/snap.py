#!/usr/bin/env python3
# a snap — merged into res.py. This shim forwards so `a snap …` (and the reboot auto-restore
# trigger in tmux.c) keep working. New work goes in res.py.
import os, sys
os.execvp("python3", ["python3", os.path.join(os.path.dirname(os.path.abspath(__file__)), "res.py")] + sys.argv[1:])
