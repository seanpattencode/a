#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path
from flask import Flask, render_template_string

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
tasks_file = aios_dir / "tasks.txt"
plan_file = aios_dir / "daily_plan.md"
status_file = aios_dir / "status.json"
pid_file = aios_dir / "web_ui.pid"

app = Flask(__name__)

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>AIOS Dashboard</title>
    <meta charset="utf-8">
    <style>
        body { font-family: monospace; background: #1e1e1e; color: #d4d4d4; margin: 20px; }
        h1 { color: #4ec9b0; }
        h2 { color: #569cd6; }
        pre { background: #2d2d30; padding: 10px; border-radius: 5px; overflow-x: auto; }
        .section { margin: 20px 0; padding: 15px; background: #252526; border-radius: 8px; }
        .status { color: #b5cea8; }
        .task { padding: 5px 0; }
        .completed { color: #6a9955; text-decoration: line-through; }
        .in-progress { color: #dcdcaa; }
        .failed { color: #f48771; }
    </style>
    <script>
        setInterval(() => location.reload(), 5000);
    </script>
</head>
<body>
    <h1>AIOS Dashboard</h1>

    <div class="section">
        <h2>Status</h2>
        <div class="status">{{ status }}</div>
    </div>

    <div class="section">
        <h2>Tasks</h2>
        <pre>{{ tasks }}</pre>
    </div>

    <div class="section">
        <h2>Daily Plan</h2>
        <pre>{{ plan }}</pre>
    </div>
</body>
</html>
"""

@app.route("/")
def index():
    tasks = tasks_file.read_text() if tasks_file.exists() else "No tasks"
    plan = plan_file.read_text() if plan_file.exists() else "No plan"
    status = json.loads(status_file.read_text()) if status_file.exists() else {}

    status_text = f"Total: {status.get('tasks_total', 0)} | Completed: {status.get('tasks_completed', 0)}"

    return render_template_string(HTML_TEMPLATE, tasks=tasks, plan=plan, status=status_text)

def start_server():
    process = subprocess.Popen(["python3", __file__, "--internal-run"],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    pid_file.write_text(str(process.pid))
    print(f"Server started on http://localhost:8080 (PID: {process.pid})")

def stop_server():
    pid = int(pid_file.read_text()) if pid_file.exists() else None
    if pid:
        subprocess.run(["kill", str(pid)])
        pid_file.unlink()
        print("Server stopped")

def server_status():
    pid = int(pid_file.read_text()) if pid_file.exists() else None
    running = subprocess.run(["kill", "-0", str(pid)], capture_output=True).returncode == 0 if pid else False
    print(f"Server: {'running' if running else 'stopped'}")

def restart_server():
    stop_server()
    start_server()

parser = argparse.ArgumentParser(description="AIOS Web UI")
parser.add_argument("--internal-run", action="store_true", help=argparse.SUPPRESS)
subparsers = parser.add_subparsers(dest="command", help="Commands")

start_parser = subparsers.add_parser("start", help="Start server")
stop_parser = subparsers.add_parser("stop", help="Stop server")
status_parser = subparsers.add_parser("status", help="Server status")
restart_parser = subparsers.add_parser("restart", help="Restart server")

args = parser.parse_args()

if args.internal_run:
    app.run(host="0.0.0.0", port=8080)
else:
    commands = {
        "start": start_server,
        "stop": stop_server,
        "status": server_status,
        "restart": restart_server
    }

    command_func = commands.get(args.command, server_status)
    command_func()