#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path
sys.path.append('/home/seanpatten/projects/AIOS')
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db

job_id = sys.argv[1]
output_file = Path.home() / ".aios" / f"autollm_output_{job_id}.txt"

cmd_type = sys.argv[2]
model = sys.argv[3]
task = " ".join(sys.argv[4:])

commands = {
    "claude": ["claude", "--dangerously-skip-permissions", task],
    "codex": ["codex", "-c", "model_reasoning_effort=high", "--model", model, "--dangerously-bypass-approvals-and-sandbox", task]
}

result = subprocess.run(commands.get(cmd_type, ["echo", "Invalid command"]), capture_output=True, text=True, timeout=999999)
output_file.write_text(result.stdout + result.stderr)
aios_db.execute("autollm", "UPDATE worktrees SET output=?, status='review' WHERE job_id=?", (result.stdout + result.stderr, job_id))
aios_db.execute("jobs", "UPDATE jobs SET output=?, status='review' WHERE id=?", (result.stdout + result.stderr, job_id))