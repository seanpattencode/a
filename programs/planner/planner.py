#!/usr/bin/env python3
import sys
sys.path.append('/home/seanpatten/projects/AIOS')
import aios_db
from datetime import datetime, timedelta

tasks = aios_db.read("tasks") or []
today = datetime.now().date()
plan = aios_db.read("daily_plan") or {}

pending = [t for t in tasks if not t.startswith("[x]") and not t.startswith("[!]")]
plan[str(today)] = pending[:10]

aios_db.write("daily_plan", plan)
[print(f"- {t}") for t in plan[str(today)]]