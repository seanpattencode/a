#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
import aios_db
import subprocess

cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
job_id = sys.argv[2] if len(sys.argv) > 2 else None

jobs = aios_db.query("jobs", "SELECT id, name, status, output FROM jobs ORDER BY created DESC")

def print_job(j):
    print(f"{j[0]}: {j[1]} - {j[2]} - {(j[3] or 'No output')[:50]}...")

def print_running(j):
    print(f'<div class="job-item">{j[1]} <span class="status running">Running...</span></div>')

def print_review(j):
    output = (j[3] or "")[:50] + "..." if j[3] else ""
    print(f'<div class="job-item">{j[1]} <span class="output">{output}</span>')
    print(f'<form action="/job/accept" method="POST" style="display:inline"><input type="hidden" name="id" value="{j[0]}"><button class="action-btn">Accept</button></form>')
    print(f'<form action="/job/redo" method="POST" style="display:inline"><input type="hidden" name="id" value="{j[0]}"><button class="action-btn">Redo</button></form></div>')

def print_done(j):
    output = (j[3] or "")[:50] + "..." if j[3] else ""
    print(f'<div class="job-item">{j[1]} <span class="output">{output}</span></div>')

if cmd == "summary":
    running = list(filter(lambda j: j[2] == "running", jobs))
    review = list(filter(lambda j: j[2] == "review", jobs))
    done = list(filter(lambda j: j[2] == "done", jobs))[:5]
    summary = []
    list(map(summary.extend, [[f"RUN {j[1]}" for j in running[:2]], [f"? {j[1]}" for j in review[:1]], [f"DONE {j[1]}" for j in done[:1]]]))
    list(map(print, summary[:4]))
elif cmd == "running":
    list(map(print_running, filter(lambda j: j[2] == "running", jobs[:10])))
elif cmd == "review":
    list(map(print_review, filter(lambda j: j[2] == "review", jobs[:10])))
elif cmd == "done":
    list(map(print_done, filter(lambda j: j[2] == "done", jobs[:50])))
elif cmd == "run_wiki":
    aios_db.execute("jobs", "INSERT INTO jobs(name, status) VALUES ('wiki', 'running')")
    new_id = aios_db.query("jobs", "SELECT MAX(id) FROM jobs")[0][0]
    subprocess.Popen(["python3", "programs/wiki_fetcher/wiki_fetcher.py", str(new_id)])
elif cmd == "accept" and job_id:
    aios_db.execute("jobs", "UPDATE jobs SET status='done' WHERE id=?", (int(job_id),))
elif cmd == "redo" and job_id:
    aios_db.execute("jobs", "UPDATE jobs SET status='running' WHERE id=?", (int(job_id),))
    subprocess.Popen(["python3", "programs/wiki_fetcher/wiki_fetcher.py", str(job_id)])
else:
    list(map(print_job, jobs[:20]))