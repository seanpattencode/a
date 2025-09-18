#!/usr/bin/env python3
"""
Force the google_drive_backup job to run by resetting its last run time.
"""
import sqlite3
import time

DB_PATH = "orchestrator.db"

conn = sqlite3.connect(DB_PATH)

# Delete the last run record to force the interval job to run
conn.execute("DELETE FROM jobs WHERE job_name = 'google_drive_backup'")
conn.commit()

print("Reset google_drive_backup job status. It should run on the next interval check.")

# Also check if we can add it as a trigger job
cursor = conn.execute("SELECT type FROM scheduled_jobs WHERE name = 'google_drive_backup'")
job = cursor.fetchone()
if job:
    print(f"Current job type: {job[0]}")
    # We could change it to support both interval and trigger
    # conn.execute("UPDATE scheduled_jobs SET type = 'interval,trigger' WHERE name = 'google_drive_backup'")
    # conn.commit()
    # print("Updated to support triggers")

conn.close()
print("Done. The backup should run within the next minute.")