#!/usr/bin/env python3
import os, sys, subprocess as sp

CLAUDE_PROMPT = "tell me a joke"
CODEX_PROMPT = "tell me a joke"

# Launch all sessions (silent if exist)
for name, cmd in [
    ('htop', 'htop'),
    ('top', 'top'),
    ('gemini', 'gemini --yolo'),
    ('codex', 'codex -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox'),
    ('codex-p', f'codex -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox "{CODEX_PROMPT}"'),
    ('claude', 'claude --dangerously-skip-permissions'),
    ('claude-p', f'claude --dangerously-skip-permissions "{CLAUDE_PROMPT}"')
]:
    sp.run(['tmux', 'new', '-d', '-s', name, cmd], capture_output=True)

s = {'h': 'htop', 't': 'top', 'g': 'gemini', 'c': 'codex', 'cp': 'codex-p', 'l': 'claude', 'lp': 'claude-p'}
arg = sys.argv[1] if len(sys.argv) > 1 else None

if not arg:
    sp.run(['tmux', 'ls'])
else:
    name = s.get(arg, arg)
    os.execvp('tmux', ['tmux', 'switch-client' if "TMUX" in os.environ else 'attach', '-t', name])
