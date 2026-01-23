#!/usr/bin/env python3
"""Test warm daemon vs cold subprocess startup times"""
import os, sys, socket, time, subprocess as sp, json, sqlite3, re, pathlib

SOCK = '/tmp/aio_warm.sock'

def daemon():
    """Pre-warmed Python daemon - fork on each request"""
    # Pre-import everything aio uses
    import subprocess, json, sqlite3, shutil, pathlib, re, shlex, atexit

    # Remove stale socket
    try: os.unlink(SOCK)
    except: pass

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(SOCK)
    sock.listen(5)
    print(f"Warm daemon listening on {SOCK}", file=sys.stderr)

    while True:
        conn, _ = sock.accept()
        data = conn.recv(8192).decode()

        if data == 'PING':
            conn.send(b'PONG')
            conn.close()
            continue

        if data == 'STOP':
            conn.close()
            break

        pid = os.fork()
        if pid == 0:  # Child
            try:
                start = time.perf_counter()
                result = eval(data)  # Execute the command
                elapsed = (time.perf_counter() - start) * 1000
                conn.send(json.dumps({'result': str(result), 'ms': elapsed}).encode())
            except Exception as e:
                conn.send(json.dumps({'error': str(e)}).encode())
            conn.close()
            os._exit(0)
        else:  # Parent
            conn.close()
            os.waitpid(pid, 0)

    os.unlink(SOCK)
    print("Daemon stopped")

def client(code):
    """Send code to warm daemon"""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCK)
    sock.send(code.encode())
    result = sock.recv(8192).decode()
    sock.close()
    return json.loads(result)

def is_daemon_running():
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCK)
        sock.send(b'PING')
        r = sock.recv(16)
        sock.close()
        return r == b'PONG'
    except:
        return False

def benchmark():
    """Compare cold subprocess vs warm daemon"""
    N = 10

    # Test 1: Cold subprocess - full Python startup
    print(f"\n=== Cold subprocess (N={N}) ===")
    cold_times = []
    for _ in range(N):
        start = time.perf_counter()
        sp.run([sys.executable, '-c', 'import json, sqlite3, subprocess; print("ok")'],
               capture_output=True)
        cold_times.append((time.perf_counter() - start) * 1000)
    print(f"  Mean: {sum(cold_times)/N:.1f}ms")
    print(f"  Min:  {min(cold_times):.1f}ms")
    print(f"  Max:  {max(cold_times):.1f}ms")

    # Test 2: Warm daemon
    if not is_daemon_running():
        print("\n[Starting daemon in background...]")
        sp.Popen([sys.executable, __file__, 'daemon'],
                 stdout=sp.DEVNULL, stderr=sp.DEVNULL)
        time.sleep(0.3)  # Wait for daemon to start

    print(f"\n=== Warm daemon (N={N}) ===")
    warm_times = []
    for _ in range(N):
        start = time.perf_counter()
        client('"ok"')  # Simple eval
        warm_times.append((time.perf_counter() - start) * 1000)
    print(f"  Mean: {sum(warm_times)/N:.1f}ms")
    print(f"  Min:  {min(warm_times):.1f}ms")
    print(f"  Max:  {max(warm_times):.1f}ms")

    # Test 3: Warm daemon with actual work
    print(f"\n=== Warm daemon + work (N={N}) ===")
    work_times = []
    for _ in range(N):
        start = time.perf_counter()
        client('subprocess.getoutput("echo hello")')
        work_times.append((time.perf_counter() - start) * 1000)
    print(f"  Mean: {sum(work_times)/N:.1f}ms")
    print(f"  Min:  {min(work_times):.1f}ms")
    print(f"  Max:  {max(work_times):.1f}ms")

    # Test 4: Cold subprocess with same work
    print(f"\n=== Cold subprocess + work (N={N}) ===")
    cold_work_times = []
    for _ in range(N):
        start = time.perf_counter()
        sp.run([sys.executable, '-c',
                'import subprocess; print(subprocess.getoutput("echo hello"))'],
               capture_output=True)
        cold_work_times.append((time.perf_counter() - start) * 1000)
    print(f"  Mean: {sum(cold_work_times)/N:.1f}ms")
    print(f"  Min:  {min(cold_work_times):.1f}ms")
    print(f"  Max:  {max(cold_work_times):.1f}ms")

    # Summary
    print(f"\n=== Summary ===")
    print(f"  Cold startup overhead: {sum(cold_times)/N:.1f}ms")
    print(f"  Warm startup overhead: {sum(warm_times)/N:.1f}ms")
    print(f"  Speedup: {sum(cold_times)/sum(warm_times):.1f}x")
    print(f"  Saved per call: {sum(cold_times)/N - sum(warm_times)/N:.1f}ms")

def background_analysis():
    """Analyze background daemon options for Mac and Ubuntu"""
    print("\n=== Background Daemon Options ===\n")

    is_mac = sys.platform == 'darwin'
    is_linux = sys.platform == 'linux'

    print(f"Platform: {'macOS' if is_mac else 'Linux' if is_linux else sys.platform}\n")

    options = []

    # Check launchd (Mac)
    if is_mac:
        options.append(("launchd (Mac native)", "~/Library/LaunchAgents/com.aio.warm.plist", """
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>com.aio.warm</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/python3</string>
        <string>""" + os.path.abspath(__file__) + """</string>
        <string>daemon</string>
    </array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
</dict>
</plist>

# Install: launchctl load ~/Library/LaunchAgents/com.aio.warm.plist
# Remove:  launchctl unload ~/Library/LaunchAgents/com.aio.warm.plist
"""))

    # Check systemd (Linux)
    if is_linux:
        options.append(("systemd (Linux native)", "~/.config/systemd/user/aio-warm.service", """
[Unit]
Description=aio warm Python daemon

[Service]
ExecStart=/usr/bin/python3 """ + os.path.abspath(__file__) + """ daemon
Restart=always

[Install]
WantedBy=default.target

# Install: systemctl --user enable --now aio-warm.service
# Remove:  systemctl --user disable --now aio-warm.service
"""))

    # Universal options
    options.append(("Shell background (any Unix)", ".bashrc / .zshrc", """
# Add to shell rc:
(pgrep -f "aio_warm.*daemon" || python3 """ + os.path.abspath(__file__) + """ daemon &) 2>/dev/null
"""))

    options.append(("tmux session (any Unix)", "manual or script", """
tmux new-session -d -s aio-warm 'python3 """ + os.path.abspath(__file__) + """ daemon'
"""))

    for name, location, config in options:
        print(f"--- {name} ---")
        print(f"Location: {location}")
        print(config)

if __name__ == '__main__':
    if len(sys.argv) > 1:
        cmd = sys.argv[1]
        if cmd == 'daemon':
            daemon()
        elif cmd == 'stop':
            if is_daemon_running():
                client('STOP')
                print("Stopped")
            else:
                print("Not running")
        elif cmd == 'status':
            print("Running" if is_daemon_running() else "Not running")
        elif cmd == 'bg':
            background_analysis()
        else:
            print(f"Unknown: {cmd}")
    else:
        benchmark()
        print("\nRun 'python3 test_warm_daemon.py bg' for background daemon options")
        print("Run 'python3 test_warm_daemon.py stop' to stop daemon")
