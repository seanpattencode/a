#!/usr/bin/env python3
"""Refactor aio.py: add timing, helpers, compress help text."""
import re

# Read original
with open('aio.py', 'r') as f:
    content = f.read()

# 1. Add timing system and helpers after imports
# Match the exact original format including comment lines
old_imports = '''#!/usr/bin/env python3
import os, sys, subprocess as sp, json
import sqlite3
from datetime import datetime
from pathlib import Path
import shlex
import shutil
import time

# Lazy-loaded optional dependencies (import on first use for fast startup)
# prompt_toolkit (~50ms) and pexpect (~5ms) are only imported when actually needed
# Note: Background thread preloading was tested but adds overhead without benefit
# because aio startup (~30ms) is faster than import time (~50ms)
_pexpect = None'''

new_imports = '''#!/usr/bin/env python3
import os, sys, subprocess as sp, json, sqlite3, shlex, shutil, time, atexit
from datetime import datetime
from pathlib import Path

_START = time.time()
_CMD = ' '.join(sys.argv[1:3]) if len(sys.argv) > 1 else 'help'

def _save_timing():
    try:
        d = os.path.expanduser("~/.local/share/aios")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "timing.jsonl"), "a") as f:
            f.write(json.dumps({"cmd": _CMD, "ms": int((time.time() - _START) * 1000), "ts": datetime.now().isoformat()}) + "\\n")
    except: pass

atexit.register(_save_timing)

# Helpers for common patterns
def _git(args, cwd=None, env=None): return sp.run(['git'] + (['-C', cwd] if cwd else []) + args, capture_output=True, text=True, env=env)
def _tmux(args): return sp.run(['tmux'] + args, capture_output=True, text=True)
def _ok(msg): print(f"✓ {msg}")
def _err(msg): print(f"✗ {msg}")
def _die(msg, code=1): _err(msg); sys.exit(code)
def _confirm(msg): return input(f"{msg} (y/n): ").strip().lower() in ['y', 'yes']

# Lazy-loaded optional dependencies
_pexpect = None'''

content = content.replace(old_imports, new_imports)

# 2. Compress the massive help text (find it by the unique header)
help_start = content.find('elif arg == \'help\' or arg == \'--help\' or arg == \'-h\':\n    print(f"""aio - AI agent session manager (DETAILED HELP)')
if help_start == -1:
    print("Could not find help text start marker")
else:
    # Find the end of the help print statement
    help_end = content.find('""")', help_start)
    if help_end != -1:
        help_end = content.find('\n', help_end) + 1  # Include the newline

        # Find where PROJECTS check starts
        projects_check = content.find('    if PROJECTS:\n        print("📁 PROJECTS', help_end)
        if projects_check != -1 and projects_check - help_end < 100:
            help_end = projects_check

        old_help = content[help_start:help_end]

        new_help = '''elif arg == 'help' or arg == '--help' or arg == '-h':
    print(f"""aio - AI agent session manager
SESSIONS: c=codex l=claude g=gemini h=htop t=top
  aio <key> [#|dir]      Start session (# = project index)
  aio <key>-- [#]        New worktree  |  aio +<key>  New timestamped
  aio cp/lp/gp           Insert prompt (edit first)
  aio cpp/lpp/gpp        Auto-run prompt
  aio <key> "prompt"     Send custom prompt  |  -w new window  -t +terminal

WORKFLOWS: aio fix|bug|feat|auto|del [agent] ["task"]
  fix=autonomous  bug=debug  feat=add  auto=improve  del=cleanup

OVERNIGHT: aio on [#] [c:N l:N]  Read aio.md, agents work, auto-review

WORKTREES: aio w  list | w<#>  open | w<#>-  delete | w<#>--  push+delete

PROJECTS: aio p  list | aio <#>  open | add/remove <#>

APPS: aio app [add|edit|rm] <name> [cmd]  Run with: aio <#>

MONITOR: jobs [-r] | review | cleanup | ls | attach | killall
  multi <#> c:N l:N "task"  Parallel agents
  send <sess> "text"  |  watch <sess> [sec]

GIT: push [file] [msg] | pull [-y] | revert [N] | setup <url>

CONFIG: install | deps | update | font [+|-|N] | config [key] [val]
  claude_prefix="Ultrathink. "  (auto-prefixes Claude prompts)

FLAGS: -w new-window  -t with-terminal  -y skip-confirm
DB: ~/.local/share/aios/aio.db  Worktrees: {WORKTREES_DIR}""")
'''
        content = content[:help_start] + new_help + content[help_end:]
        print(f"Compressed help text: {len(old_help)} -> {len(new_help)} chars")

# 3. Remove redundant comments
content = re.sub(r'\n# Note:.*\n', '\n', content)
content = re.sub(r'\n    # Note:.*\n', '\n', content)

# 4. Remove the commented-out 'files' service section (~160 lines)
files_start = content.find('# ═══════════════════════════════════════════════════════════════════════════════\n# LOCAL FILES SERVICE')
if files_start != -1:
    files_end = content.find('# ═══════════════════════════════════════════════════════════════════════════════\n\nelif arg ==', files_start)
    if files_end != -1:
        files_end = content.find('\n\n', files_end) + 2
        old_len = files_end - files_start
        content = content[:files_start] + content[files_end:]
        print(f"Removed commented-out files service: {old_len} chars")

# 5. Combine consecutive print statements into one (saves many lines)
def combine_prints(content):
    lines = content.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        # Check for start of consecutive prints (at least 3)
        if re.match(r'^(\s+)print\(["\']', line):
            indent = re.match(r'^(\s+)', line).group(1)
            prints = [line]
            j = i + 1
            while j < len(lines) and re.match(rf'^{indent}print\(["\']', lines[j]):
                prints.append(lines[j])
                j += 1
            if len(prints) >= 4:  # Only combine if 4+ consecutive prints
                # Extract strings and combine
                strings = []
                for p in prints:
                    m = re.search(r'print\(f?["\'](.+)["\'].*\)', p)
                    if m:
                        strings.append(m.group(1))
                if strings:
                    combined = f'{indent}print("""' + '\\n'.join(strings) + '""")'
                    result.append(combined)
                    i = j
                    continue
        result.append(line)
        i += 1
    return '\n'.join(result)

content = combine_prints(content)

# 6. Remove empty lines after opening braces/colons (more compact)
content = re.sub(r':\n\n(\s+)', r':\n\1', content)

# 6. Combine consecutive single-statement lines where safe
# e.g., "x = 1\ny = 2" on related assignments

# 7. Remove standalone pass statements that aren't needed
content = re.sub(r'\n(\s+)pass\n(\s+)(def |class |elif |else:)', r'\n\2\3', content)

# 8. Compress simple if-else to ternary where obvious
# This is risky so skip for now

# 9. Remove trailing whitespace
content = re.sub(r' +\n', '\n', content)

# 10. Remove multiple blank lines
content = re.sub(r'\n\n\n+', '\n\n', content)

# 11. Remove blank lines before 'else:', 'elif', 'except:', 'finally:'
content = re.sub(r'\n\n(\s+)(else:|elif |except:|finally:)', r'\n\1\2', content)

# 12. Remove blank lines after function/class definition before first statement
content = re.sub(r'(def \w+\([^)]*\):)\n\n(\s+)', r'\1\n\2', content)
content = re.sub(r'(class \w+[^:]*:)\n\n(\s+)', r'\1\n\2', content)

# 13. Remove redundant imports inside functions (just use __import__)
# This is risky, skip for now

# 14. Skip if combining - too risky

# 15. Remove # TODO comments
content = re.sub(r'\n\s*# TODO[^\n]*', '', content)

# 16. Remove # FIXME comments
content = re.sub(r'\n\s*# FIXME[^\n]*', '', content)

# 17. Remove verbose continuation comments like "# ... rest of logic"
content = re.sub(r'\n\s*# \.\.\.[^\n]*', '', content)

# 18. More aggressive blank line removal - only allow one blank between code blocks
lines = content.split('\n')
result = []
prev_blank = False
for line in lines:
    is_blank = not line.strip()
    if is_blank and prev_blank:
        continue  # Skip consecutive blanks
    result.append(line)
    prev_blank = is_blank
content = '\n'.join(result)

# 19. Remove comments that just repeat the code (like "# Exit" before sys.exit)
content = re.sub(r'\n\s*# (Exit|Return|Continue|Break|Stop|Done)\s*\n', '\n', content, flags=re.IGNORECASE)

# Write result
with open('aio.py', 'w') as f:
    f.write(content)

# Count lines
lines = content.count('\n') + 1
print(f"Final line count: {lines}")
