#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db

job_id = (sys.argv + [""])[1]
output_file = Path.home() / ".aios" / f"autollm_output_{job_id}.txt"
db_output = aios_db.query("autollm", "SELECT output FROM worktrees WHERE job_id=?", (job_id,))

print(output_file.read_text() * output_file.exists() or (db_output[0][0] or "No output yet") * bool(db_output) or "No output yet")