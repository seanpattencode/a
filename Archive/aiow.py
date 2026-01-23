#!/usr/bin/env python3
"""aio warm daemon - pre-runs aio to cache all imports"""
import os, sys, socket, io

SOCK = '/tmp/aiow.sock'
AIO = os.path.join(os.path.dirname(__file__), 'aio.py')

if len(sys.argv) > 1 and sys.argv[1] == 'daemon':
    # Pre-compile
    code = compile(open(AIO).read(), AIO, 'exec')

    # Pre-run with 'help' to trigger all imports, discard output
    sys.argv = ['aio', 'help']
    sys.stdout = io.StringIO()
    sys.stderr = io.StringIO()
    exec(code, {'__name__': '__main__', '__file__': AIO})
    sys.stdout = sys.__stdout__
    sys.stderr = sys.__stderr__

    # Now all imports are cached in sys.modules
    try: os.unlink(SOCK)
    except: pass

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(SOCK)
    sock.listen(5)
    print("aiow ready (pre-warmed)")

    while True:
        conn, _ = sock.accept()
        args = conn.recv(4096).decode().strip()
        if args == 'STOP': conn.close(); break

        if os.fork() == 0:
            # Parse CWD if provided
            if args.startswith('CWD:'):
                lines = args.split('\n', 1)
                cwd = lines[0][4:]
                args = lines[1] if len(lines) > 1 else ''
                os.chdir(cwd)
            buf = io.StringIO()
            sys.stdout = buf
            sys.stderr = buf
            sys.argv = ['aio'] + (args.split() if args else [])
            try:
                exec(code, {'__name__': '__main__', '__file__': AIO})
            except SystemExit:
                pass
            except Exception as e:
                buf.write(f"Error: {e}")
            conn.send(buf.getvalue().encode())
            conn.close()
            os._exit(0)
        else:
            os.waitpid(-1, os.WNOHANG)
            conn.close()

    os.unlink(SOCK)
    sys.exit(0)

# Client (if not using shell client)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try: sock.connect(SOCK)
except: print("Start: python3 aiow.py daemon &"); sys.exit(1)
sock.send(' '.join(sys.argv[1:]).encode())
sock.shutdown(socket.SHUT_WR)
while (d := sock.recv(4096)): sys.stdout.write(d.decode()); sys.stdout.flush()
