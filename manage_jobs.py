#!/usr/bin/env python3
"""
Utility script to manage scheduled jobs in the AIOS orchestrator database.
"""
import sqlite3
import json
import sys
from pathlib import Path

DB_PATH = Path(__file__).parent / "orchestrator.db"

def list_jobs():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.execute("SELECT * FROM scheduled_jobs ORDER BY name")

    print("\nScheduled Jobs:")
    print("-" * 80)
    for row in cursor:
        tags = json.loads(row["tags"]) if row["tags"] else []
        print(f"Name: {row['name']}")
        print(f"  Type: {row['type']}")
        print(f"  File: {row['file']}")
        print(f"  Function: {row['function']}")
        print(f"  Enabled: {'Yes' if row['enabled'] else 'No'}")
        if tags:
            print(f"  Tags: {', '.join(tags)}")
        if row["time"]:
            print(f"  Time: {row['time']}")
        if row["interval_minutes"]:
            print(f"  Interval: {row['interval_minutes']} minutes")
        if row["retries"]:
            print(f"  Retries: {row['retries']}")
        print()
    conn.close()

def enable_job(job_name):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("UPDATE scheduled_jobs SET enabled = 1 WHERE name = ?", (job_name,))
    conn.commit()
    conn.close()
    print(f"Job '{job_name}' enabled")

def disable_job(job_name):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("UPDATE scheduled_jobs SET enabled = 0 WHERE name = ?", (job_name,))
    conn.commit()
    conn.close()
    print(f"Job '{job_name}' disabled")

def add_job(name, file, function, job_type, **kwargs):
    conn = sqlite3.connect(DB_PATH)
    try:
        conn.execute("""
            INSERT INTO scheduled_jobs
            (name, file, function, type, tags, retries, time, after_time, before_time, interval_minutes, priority, enabled)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            name,
            file,
            function,
            job_type,
            json.dumps(kwargs.get("tags", [])),
            kwargs.get("retries", 3),
            kwargs.get("time"),
            kwargs.get("after_time"),
            kwargs.get("before_time"),
            kwargs.get("interval_minutes"),
            kwargs.get("priority", 0),
            1
        ))
        conn.commit()
        print(f"Job '{name}' added successfully")
    except sqlite3.IntegrityError:
        print(f"Job '{name}' already exists")
    finally:
        conn.close()

def remove_job(job_name):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("DELETE FROM scheduled_jobs WHERE name = ?", (job_name,))
    conn.commit()
    conn.close()
    print(f"Job '{job_name}' removed")

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python manage_jobs.py list                - List all jobs")
        print("  python manage_jobs.py enable <job_name>   - Enable a job")
        print("  python manage_jobs.py disable <job_name>  - Disable a job")
        print("  python manage_jobs.py remove <job_name>   - Remove a job")
        print("  python manage_jobs.py add <name> <file> <function> <type> [options]")
        return

    command = sys.argv[1]

    if command == "list":
        list_jobs()
    elif command == "enable" and len(sys.argv) >= 3:
        enable_job(sys.argv[2])
    elif command == "disable" and len(sys.argv) >= 3:
        disable_job(sys.argv[2])
    elif command == "remove" and len(sys.argv) >= 3:
        remove_job(sys.argv[2])
    elif command == "add" and len(sys.argv) >= 5:
        # Basic add functionality
        add_job(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        print("Invalid command or missing arguments")

if __name__ == "__main__":
    main()