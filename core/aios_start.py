#!/usr/bin/env python3
import subprocess, time, sys, socket, webbrowser
from pathlib import Path
sys.path.append('/home/seanpatten/projects/AIOS')
from core import aios_db

def find_free_port(start_port=8080, max_attempts=10):
    """Find a free port starting from start_port"""
    for port in range(start_port, start_port + max_attempts):
        try:
            test_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            test_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            test_sock.bind(('', port))
            test_sock.close()
            return port
        except OSError:
            continue
    raise RuntimeError(f"No free ports found between {start_port} and {start_port + max_attempts}")

def find_free_api_port(start_port=8000, max_attempts=10):
    """Find a free port for API starting from start_port"""
    for port in range(start_port, start_port + max_attempts):
        try:
            test_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            test_sock.bind(('', port))
            test_sock.close()
            return port
        except OSError:
            continue
    raise RuntimeError(f"No free API ports found between {start_port} and {start_port + max_attempts}")

aios_path = Path.home() / ".aios"
start_time = time.time()

# Don't kill existing instances - allow multiple sessions
# subprocess.run(["pkill", "-9", "-f", "core/aios_api.py"], stderr=subprocess.DEVNULL)
# subprocess.run(["pkill", "-9", "-f", "services/web/web.py"], stderr=subprocess.DEVNULL)

aios_path.mkdir(exist_ok=True)

# Find available ports
web_port = find_free_port(8080)
api_port = find_free_api_port(8000)

# Save port info
aios_db.write("ports", {"web": web_port, "api": api_port})
aios_db.write("aios_pids", {})

# Create web socket
web_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
web_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
web_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
web_sock.bind(('', web_port))
web_sock.listen(5)

# Start API with dynamic port
api_proc = subprocess.Popen(["python3", "core/aios_api.py", str(api_port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Start web server with socket and port info
web_proc = subprocess.Popen(["python3", "services/web/web.py", str(web_sock.fileno()), str(web_port), str(api_port)], pass_fds=[web_sock.fileno()])
web_sock.close()

aios_db.write("aios_pids", {"api": api_proc.pid, "web": web_proc.pid})
elapsed = time.time() - start_time
print(f"AIOS started in {elapsed:.3f}s: http://localhost:{web_port}")
webbrowser.open(f"http://localhost:{web_port}")
subprocess.Popen(["python3", "-c", "from services import context_generator; context_generator.generate()"], cwd="/home/seanpatten/projects/AIOS")