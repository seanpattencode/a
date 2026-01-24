#!/usr/bin/env python3
"""aio - modular version (git-like structure)"""
import sys

# Command aliases (like git's cmd_struct)
CMDS = {
    None: 'help', '': 'help', 'help': 'help', 'hel': 'help', '--help': 'help', '-h': 'help',
    'diff': 'diff', 'dif': 'diff',
    'ls': 'ls',
}

def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    cmd = CMDS.get(arg)

    if cmd:
        # Lazy import - only load the command module when needed
        mod = __import__(f'aio_cmd.{cmd}', fromlist=[cmd])
        mod.run()
    else:
        # Fallback to monolith for unknown commands
        import aio

if __name__ == '__main__':
    main()
