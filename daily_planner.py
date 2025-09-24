#!/usr/bin/env python3
import argparse
import sqlite3
import json
from openai import OpenAI
from pathlib import Path
from datetime import datetime

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
goals_file = aios_dir / "goals.txt"
tasks_file = aios_dir / "tasks.txt"
plan_file = aios_dir / "daily_plan.md"
events_db = aios_dir / "events.db"
config_file = aios_dir / "llm_config.json"

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

goals_file.touch(exist_ok=True)
tasks_file.touch(exist_ok=True)

config = json.loads(config_file.read_text()) if config_file.exists() else {"api_key": ""}
client = OpenAI(api_key=config.get("api_key", ""))

def check_replan_events():
    conn = sqlite3.connect(events_db)
    events = conn.execute(
        "SELECT * FROM events WHERE target='daily_planner' AND type='need_replan' AND processed_by IS NULL"
    ).fetchall()

    for event in events:
        conn.execute("UPDATE events SET processed_by='daily_planner' WHERE id=?", (event[0],))

    conn.commit()
    conn.close()
    return len(events) > 0

def generate_plan(energy_level="normal"):
    goals = goals_file.read_text()
    tasks = tasks_file.read_text()

    prompt = f"""Create a time-blocked daily schedule.
    Energy: {energy_level}
    Goals: {goals}
    Tasks: {tasks}
    Format as markdown with time blocks (8am-6pm)."""

    response = client.chat.completions.create(
        model="gpt-3.5-turbo",
        messages=[{"role": "user", "content": prompt}],
        max_tokens=500
    )

    plan = response.choices[0].message.content
    plan_file.write_text(plan)
    return plan

def plan_command():
    energy = input("Energy level (low/normal/high): ") or "normal"
    plan = generate_plan(energy)
    print(plan)

def replan_command():
    needs_replan = check_replan_events()
    plan = generate_plan("adjusted") if needs_replan else plan_file.read_text()
    print("Replanned due to skipped tasks." if needs_replan else "No replan needed.")
    print(plan)

def goals_command():
    print(goals_file.read_text() or "No goals set.")

def status_command():
    plan = plan_file.read_text() if plan_file.exists() else "No plan generated."
    print(plan)

parser = argparse.ArgumentParser(description="AIOS Daily Planner")
subparsers = parser.add_subparsers(dest="command", help="Commands")

plan_parser = subparsers.add_parser("plan", help="Generate daily plan")
replan_parser = subparsers.add_parser("replan", help="Check and replan if needed")
goals_parser = subparsers.add_parser("goals", help="Show goals")
status_parser = subparsers.add_parser("status", help="Show current plan")

args = parser.parse_args()

commands = {
    "plan": plan_command,
    "replan": replan_command,
    "goals": goals_command,
    "status": status_command
}

command_func = commands.get(args.command, status_command)
command_func()