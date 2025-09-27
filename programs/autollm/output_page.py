#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db

job_id = (sys.argv + [""])[1]
output_file = Path.home() / ".aios" / f"autollm_output_{job_id}.txt"
worktree = aios_db.query("autollm", "SELECT branch, task, model, status, output FROM worktrees WHERE job_id=?", (job_id,))

info = worktree[0] * bool(worktree) or ["unknown", "unknown", "unknown", "unknown", ""]
file_output = output_file.read_text() * output_file.exists() or (info[4] or "No output yet") * bool(worktree) or "No output yet"

print(f"Branch: {info[0]}")
print(f"Task: {info[1]}")
print(f"Model: {info[2]}")
print(f"Status: {info[3]}")
print(f"Output:\n{file_output}")