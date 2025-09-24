#!/usr/bin/env python3
import argparse
import subprocess
import sqlite3
import json
from pathlib import Path
from datetime import datetime

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
components_dir = aios_dir / "components"
components_dir.mkdir(exist_ok=True)
events_db = aios_dir / "events.db"
build_status_file = aios_dir / "build_status.json"

conn = sqlite3.connect(events_db)
conn.execute("""
    CREATE TABLE IF NOT EXISTS events(
        id INTEGER PRIMARY KEY,
        source TEXT,
        target TEXT,
        type TEXT,
        data TEXT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        processed_by TEXT
    )
""")
conn.close()

def emit_event(target, event_type, data):
    conn = sqlite3.connect(events_db)
    conn.execute(
        "INSERT INTO events(source, target, type, data) VALUES (?, ?, ?, ?)",
        ("parallel_builder", target, event_type, json.dumps(data))
    )
    conn.commit()
    conn.close()

def build_components(component_names):
    processes = []
    status = {}

    for name in component_names:
        cmd = ["python", "-m", "cli_ai", f"build {name} component"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append((name, proc))
        status[name] = "building"

    build_status_file.write_text(json.dumps(status, indent=2))

    for name, proc in processes:
        stdout, stderr = proc.communicate()
        success = proc.returncode == 0

        output_file = components_dir / f"{name}.py"
        output_file.write_text(stdout.decode())

        status[name] = "success" if success else "failed"
        build_status_file.write_text(json.dumps(status, indent=2))

        emit_event("ALL", "build_completed", {"component": name, "success": success})
        print(f"{name}: {'✓' if success else '✗'}")

def status_command():
    status = json.loads(build_status_file.read_text()) if build_status_file.exists() else {}
    for component, state in status.items():
        print(f"{component}: {state}")

def list_command():
    components = list(components_dir.glob("*.py"))
    for comp in components:
        print(comp.stem)

def clean_command():
    for file in components_dir.glob("*.py"):
        file.unlink()
    build_status_file.unlink(missing_ok=True)
    print("Cleaned components directory")

parser = argparse.ArgumentParser(description="AIOS Component Builder")
subparsers = parser.add_subparsers(dest="command", help="Commands")

build_parser = subparsers.add_parser("build", help="Build components")
build_parser.add_argument("components", nargs="+", help="Component names")
status_parser = subparsers.add_parser("status", help="Show build status")
list_parser = subparsers.add_parser("list", help="List built components")
clean_parser = subparsers.add_parser("clean", help="Clean components")

args = parser.parse_args()

commands = {
    "build": lambda: build_components(args.components),
    "status": status_command,
    "list": list_command,
    "clean": clean_command
}

command_func = commands.get(args.command, list_command)
command_func()