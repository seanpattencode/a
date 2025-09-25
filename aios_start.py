#!/usr/bin/env python3
import subprocess
import time
from pathlib import Path
import webbrowser
import os
import signal
import sys
sys.path.append('/home/seanpatten/projects/AIOS')
from core import aios_db
from services import context_generator

aios_path = Path.home() / ".aios"
command = sys.argv[1] if len(sys.argv) > 1 else "start"

def kill_existing():
    subprocess.run(["pkill", "-f", "core/aios_api.py"], stderr=subprocess.DEVNULL)
    subprocess.run(["pkill", "-f", "services/web.py"], stderr=subprocess.DEVNULL)
    pids = aios_db.read("aios_pids") or {}
    [[os.kill(pid, signal.SIGTERM)] for pid in pids.values() if pid and subprocess.run(["kill", "-0", str(pid)], capture_output=True).returncode == 0]

def start():
    context_generator.generate()
    kill_existing()
    aios_path.mkdir(exist_ok=True)
    api_proc = subprocess.Popen(["python3", "core/aios_api.py"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    web_proc = subprocess.Popen(["python3", "services/web.py", "start"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    aios_db.write("aios_pids", {"api": api_proc.pid, "web": web_proc.pid})
    time.sleep(0.1)
    info = aios_db.read("web_server") or {}
    url = f"http://localhost:{info.get('port', 8080)}"
    print(f"AIOS started: {url}")
    webbrowser.open(url)
    [[time.sleep(1)] for _ in iter(int, 1)]

def stop():
    kill_existing()
    aios_db.write("aios_pids", {})
    print("AIOS stopped")

actions = {"start": start, "stop": stop, "status": lambda: print(f"PIDs: {aios_db.read('aios_pids')}")}
actions.get(command, start)()