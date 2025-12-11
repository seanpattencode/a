#!/usr/bin/env python3
"""Aggressively strip non-essential comments from aio.py"""
import re

with open('aio.py', 'r') as f:
    lines = f.readlines()

result = []
i = 0
removed = 0

# Essential comment patterns to keep
KEEP_PATTERNS = [
    r'^#!/',           # shebang
    r'# type:',        # type hints
    r'# noqa',         # linter directives
    r'# pragma',       # coverage directives
    r'# fmt:',         # formatter directives
    r'# pylint',       # linter directives
    r'# TODO',         # keep todos (user might want these)
]

# Patterns that indicate the comment is needed (explains non-obvious code)
CONTEXT_KEEP = [
    r'# \d+',          # numbered steps
    r'# Step',         # step markers
    r'# IMPORTANT',    # important notes
    r'# WARNING',      # warnings
    r'# HACK',         # hacks that need explanation
    r'# XXX',          # needs attention
]

while i < len(lines):
    line = lines[i]
    stripped = line.strip()

    # Keep non-comment lines
    if not stripped.startswith('#') or stripped == '#':
        result.append(line)
        i += 1
        continue

    # Keep shebang
    if stripped.startswith('#!'):
        result.append(line)
        i += 1
        continue

    # Keep essential patterns
    keep = False
    for pattern in KEEP_PATTERNS + CONTEXT_KEEP:
        if re.search(pattern, stripped, re.IGNORECASE):
            keep = True
            break

    if keep:
        result.append(line)
        i += 1
        continue

    # Remove section dividers (═══ lines)
    if '═' in stripped or '━' in stripped or '───' in stripped:
        removed += 1
        i += 1
        continue

    # Remove single-line comments that just describe the next line
    # (these are usually redundant)
    if i + 1 < len(lines):
        next_line = lines[i + 1].strip()
        # If next line is code (not comment, not blank), comment might be redundant
        if next_line and not next_line.startswith('#'):
            # Check if comment is very short and generic
            comment_text = stripped[1:].strip().lower()
            generic_words = ['check', 'get', 'set', 'create', 'add', 'remove', 'update',
                           'load', 'save', 'init', 'setup', 'cleanup', 'handle', 'process',
                           'parse', 'build', 'run', 'execute', 'call', 'return', 'exit',
                           'for', 'if', 'else', 'try', 'except', 'with', 'while',
                           'start', 'end', 'begin', 'finish', 'done', 'continue', 'break']

            # Remove if comment is just a single generic word or very short
            words = comment_text.split()
            if len(words) <= 3:
                first_word = words[0] if words else ''
                if first_word in generic_words or len(comment_text) < 20:
                    removed += 1
                    i += 1
                    continue

    # Keep other comments
    result.append(line)
    i += 1

# Write result
with open('aio.py', 'w') as f:
    f.writelines(result)

print(f"Removed {removed} comment lines")
print(f"Final line count: {len(result)}")
