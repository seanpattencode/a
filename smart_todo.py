#!/usr/bin/env python3
import argparse
import sqlite3
import json
from pathlib import Path
from datetime import datetime

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
tasks_file = aios_dir / "tasks.txt"
status_file = aios_dir / "status.json"
events_db = aios_dir / "events.db"

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

tasks_file.touch(exist_ok=True)

def emit_event(target, event_type, data):
    conn = sqlite3.connect(events_db)
    conn.execute(
        "INSERT INTO events(source, target, type, data) VALUES (?, ?, ?, ?)",
        ("smart_todo", target, event_type, json.dumps(data))
    )
    conn.commit()
    conn.close()

def update_status():
    tasks = tasks_file.read_text().splitlines()
    total = len(tasks)
    completed = sum(1 for t in tasks if t.startswith("[x]"))
    status = {
        "tasks_total": total,
        "tasks_completed": completed,
        "last_update": datetime.now().isoformat()
    }
    status_file.write_text(json.dumps(status, indent=2))

def list_tasks():
    tasks = tasks_file.read_text().splitlines()
    for i, task in enumerate(tasks, 1):
        print(f"{i}. {task}")
    update_status()

def add_task(task_text):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    new_task = f"[ ] {timestamp} p:med {task_text}"
    existing = tasks_file.read_text()
    tasks_file.write_text(existing + new_task + "\n")
    update_status()
    print(f"Added: {new_task}")

def done_task(task_id):
    tasks = tasks_file.read_text().splitlines()
    task_idx = int(task_id) - 1
    tasks[task_idx] = tasks[task_idx].replace("[ ]", "[x]").replace("[>]", "[x]").replace("[!]", "[x]")
    tasks_file.write_text("\n".join(tasks) + "\n")
    update_status()
    print(f"Completed: {tasks[task_idx]}")

def skip_task(task_id):
    tasks = tasks_file.read_text().splitlines()
    task_idx = int(task_id) - 1
    task = tasks[task_idx]
    tasks[task_idx] = task.replace("[ ]", "[!]").replace("[>]", "[!]")
    tasks_file.write_text("\n".join(tasks) + "\n")

    priority_marker = "p:crit" in task or "p:high" in task
    emit_event("daily_planner", "need_replan", {"task": task, "critical": priority_marker})
    update_status()
    print(f"Skipped: {task}")

parser = argparse.ArgumentParser(description="AIOS Task Manager")
subparsers = parser.add_subparsers(dest="command", help="Commands")

list_parser = subparsers.add_parser("list", help="List all tasks")
add_parser = subparsers.add_parser("add", help="Add new task")
add_parser.add_argument("task", nargs="+", help="Task description")
done_parser = subparsers.add_parser("done", help="Mark task complete")
done_parser.add_argument("id", help="Task ID")
skip_parser = subparsers.add_parser("skip", help="Skip task")
skip_parser.add_argument("id", help="Task ID")

args = parser.parse_args()

commands = {
    "list": lambda: list_tasks(),
    "add": lambda: add_task(" ".join(args.task)),
    "done": lambda: done_task(args.id),
    "skip": lambda: skip_task(args.id)
}

command_func = commands.get(args.command, list_tasks)
command_func()