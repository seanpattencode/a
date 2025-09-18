#!/usr/bin/env python3
import sqlite3
import json
from datetime import datetime

conn = sqlite3.connect('/home/ubuntu/AIOS/orchestrator.db')
conn.row_factory = sqlite3.Row

print("=== Jobs Table Schema ===")
cursor = conn.execute('PRAGMA table_info(jobs)')
for row in cursor:
    print(f"  {row[1]} ({row[2]})")

print("\n=== Recent Job Runs ===")
cursor = conn.execute('SELECT * FROM jobs WHERE job_name="google_drive_backup" ORDER BY last_update DESC LIMIT 5')
for row in cursor:
    print(dict(row))

print("\n=== Recent Triggers ===")
cursor = conn.execute('SELECT * FROM triggers ORDER BY created DESC LIMIT 5')
for row in cursor:
    print(f"ID: {row['id']}, Job: {row['job_name']}, Created: {datetime.fromtimestamp(row['created'])}, Processed: {datetime.fromtimestamp(row['processed']) if row['processed'] else 'None'}")

print("\n=== Recent Logs ===")
cursor = conn.execute('SELECT * FROM logs WHERE message LIKE "%backup%" OR message LIKE "%google%" ORDER BY timestamp DESC LIMIT 10')
for row in cursor:
    print(f"{row['timestamp']}: [{row['level']}] {row['message']}")

conn.close()