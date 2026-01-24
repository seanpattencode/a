"""aio i - Interactive command picker with live suggestions"""
import sys, os, tty, termios, time, subprocess as sp

CMDS = ['help','update','jobs','kill','attach','cleanup','config','ls','diff','send','watch',
        'push','pull','revert','set','settings','install','uninstall','deps','prompt','gdrive',
        'add','remove','move','dash','all','backup','scan','copy','log','done','agent','tree',
        'dir','web','ssh','run','hub','daemon','ui','review','note']

# Build prefix dict
PREFIX = {}
for c in CMDS:
    for i in range(1, len(c)+1):
        PREFIX.setdefault(c[:i], []).append(c)

def getch():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try: tty.setraw(fd); return sys.stdin.read(1)
    finally: termios.tcsetattr(fd, termios.TCSADRAIN, old)

def run():
    if not sys.stdin.isatty():
        print("x Interactive mode requires tty"); sys.exit(1)

    buf, sel = "", 0
    print("Type to filter, Tab=cycle, Enter=run, Esc=quit\n")

    while True:
        matches = PREFIX.get(buf, CMDS)[:8] if buf else CMDS[:8]
        sel = min(sel, len(matches)-1) if matches else 0

        # Render
        sys.stdout.write(f"\r\033[K> {buf}")
        sys.stdout.write(f"\n\033[K  ")
        for i, m in enumerate(matches):
            if i == sel: sys.stdout.write(f"\033[7m {m} \033[0m ")
            else: sys.stdout.write(f" {m}  ")
        sys.stdout.write(f"\033[A\r\033[{len(buf)+3}C")
        sys.stdout.flush()

        ch = getch()
        if ch == '\x1b':  # Esc or arrow
            n = sys.stdin.read(2) if sys.stdin in __import__('select').select([sys.stdin],[],[],0)[0] else ''
            if n == '[A': sel = max(0, sel-1)  # Up
            elif n == '[B': sel = min(len(matches)-1, sel+1)  # Down
            elif n == '[C': sel = min(len(matches)-1, sel+1)  # Right
            elif n == '[D': sel = max(0, sel-1)  # Left
            elif not n: break  # Plain Esc
        elif ch == '\t': sel = (sel + 1) % len(matches) if matches else 0
        elif ch == '\x7f' and buf: buf, sel = buf[:-1], 0
        elif ch == '\r' and matches:
            cmd = matches[sel]
            print(f"\n\n\033[KRunning: aio {cmd}\n")
            os.execvp(sys.executable, [sys.executable, os.path.dirname(__file__) + '/../aio.py', cmd])
        elif ch.isalnum() or ch in '-_': buf, sel = buf + ch, 0

        sys.stdout.write("\033[1B\r")

    print("\033[2B\033[K")
