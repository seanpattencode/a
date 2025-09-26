#!/usr/bin/env python3
import sys
sys.path.append('/home/seanpatten/projects/AIOS/core')
import subprocess
import aios_db

target = sys.argv[1] if len(sys.argv) > 1 else "all"

files = {
    "tasks": f"{aios_db.db_path}/tasks.json",
    "jobs": f"{aios_db.db_path}/jobs.db",
    "feed": f"{aios_db.db_path}/feed.db",
    "settings": f"{aios_db.db_path}/settings.json",
    "schedule": f"{aios_db.db_path}/schedule.json"
}

target_file = files.get(target, " ".join(files.values()))
subprocess.run(f"inotifywait -m -e modify {target_file}", shell=True, stdout=subprocess.PIPE, text=True)