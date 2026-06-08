#!/usr/bin/env python3
"""a ask [c|x|g|k] <prompt> — one-shot CLI-LLM (-p) service; streams the answer to stdout.
  c=claude  x=codex  g=gemini  k=grok  (default c). Prompt from args or stdin.
Reusable primitive: fan one question to many models by calling this per engine (i ask)."""
import sys, os, subprocess
MAP = {'c': ['claude', '-p', '--dangerously-skip-permissions'], 'x': ['codex', 'exec'],
       'g': ['gemini', '-p'], 'k': ['grok', '-p']}
a = sys.argv[2:]                                        # a.c passes the subcommand as argv[1]
eng = a.pop(0) if a and a[0] in MAP else 'c'
args = bool(a)                                          # prompt in args -> don't make the child wait on stdin
p = ' '.join(a) or sys.stdin.read().strip()
if not p: sys.exit('usage: a ask [c|x|g|k] <prompt>   (default c=claude; prompt also via stdin)')
env = {k: v for k, v in os.environ.items() if k not in ('CLAUDECODE', 'CLAUDE_CODE_ENTRYPOINT')}
sys.exit(subprocess.run(MAP[eng] + [p], env=env, stdin=subprocess.DEVNULL if args else None).returncode)
