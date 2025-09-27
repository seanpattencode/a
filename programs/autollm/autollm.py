#!/usr/bin/env python3
import subprocess
import sys
import time
from pathlib import Path
sys.path.append('/home/seanpatten/projects/AIOS')
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db
import pty
import os
import termios
import tty
import select

command = (sys.argv + ["run"])[1]
repo_path = (sys.argv + ["", Path.cwd()])[2]
model = (sys.argv + ["", "claude-3-5-sonnet-20241022"])[3]

aios_db.execute("autollm", "CREATE TABLE IF NOT EXISTS worktrees(id INTEGER PRIMARY KEY, branch TEXT, path TEXT, job_id INTEGER, status TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")
aios_db.execute("autollm", "CREATE TABLE IF NOT EXISTS models(id INTEGER PRIMARY KEY, name TEXT, params TEXT)")

def run():
    branches = int((sys.argv + ["", "1"])[4])
    task = " ".join(sys.argv[5:]) or "Improve this code"
    base_path = Path(repo_path).resolve()
    worktrees = []

    list(map(lambda i: worktrees.append(create_worktree(base_path, f"autollm-{int(time.time())}-{i}")), range(branches)))
    list(map(lambda w: launch_job(w, task), worktrees))
    print(f"Launched {branches} worktrees")

def create_worktree(base, branch):
    worktree_path = base.parent / f"{base.name}-{branch}"
    subprocess.run(["git", "worktree", "add", "-b", branch, str(worktree_path)], cwd=base, capture_output=True)
    aios_db.execute("autollm", "INSERT INTO worktrees(branch, path, status) VALUES (?, ?, 'created')", (branch, str(worktree_path)))
    return {"branch": branch, "path": str(worktree_path)}

def launch_job(worktree, task):
    job_id = aios_db.query("jobs", "SELECT MAX(id) FROM jobs")[0][0] or 0
    aios_db.execute("jobs", "INSERT INTO jobs(name, status) VALUES (?, 'running')", (f"autollm-{worktree['branch']}",))
    new_job_id = aios_db.query("jobs", "SELECT MAX(id) FROM jobs")[0][0]
    aios_db.execute("autollm", "UPDATE worktrees SET job_id=?, status='running' WHERE branch=?", (new_job_id, worktree['branch']))

    cmd = build_command(task, worktree['path'])
    subprocess.Popen(["python3", "core/aios_runner.py"] + cmd, cwd="/home/seanpatten/projects/AIOS")

def build_command(task, cwd):
    params = aios_db.query("autollm", "SELECT params FROM models WHERE name=?", (model,))
    extra_params = params[0][0] if params else ""
    return ["codex", "-c", f"model_reasoning_effort=high", task, "--model", model] + extra_params.split()

def terminal():
    worktrees = aios_db.query("autollm", "SELECT branch, path, job_id FROM worktrees WHERE status='running'")
    print("\n".join([f"{i+1}. {w[0]} - Job {w[2]}" for i, w in enumerate(worktrees)]))

    choice = int(input("Select worktree: ")) - 1
    branch, path, job_id = worktrees[choice]
    terminal_branch(branch)

def terminal_branch(branch=None):
    branch = branch or sys.argv[2]
    worktree = aios_db.query("autollm", "SELECT path FROM worktrees WHERE branch=?", (branch,))
    path = worktree[0][0]

    master, slave = pty.openpty()
    pid = os.fork()

    if pid == 0:
        os.setsid()
        os.dup2(slave, 0)
        os.dup2(slave, 1)
        os.dup2(slave, 2)
        os.close(master)
        os.close(slave)
        os.execvp("bash", ["bash", "-c", f"cd {path} && bash"])

    os.close(slave)
    old_settings = termios.tcgetattr(sys.stdin)
    tty.setraw(sys.stdin)

    while True:
        r, w, e = select.select([sys.stdin, master], [], [])
        if sys.stdin in r:
            d = os.read(sys.stdin.fileno(), 10240)
            os.write(master, d)
        if master in r:
            d = os.read(master, 10240)
            if not d:
                break
            os.write(sys.stdout.fileno(), d)

    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)

def review():
    jobs = aios_db.query("autollm", "SELECT w.branch, w.path, j.output FROM worktrees w JOIN jobs j ON w.job_id=j.id WHERE j.status='review'")
    list(map(lambda x: print(f"{x[0]+1}. {x[1][0]} - {(x[1][2] or '')[:50]}"), enumerate(jobs)))

    choice = int(input("Select job: ")) - 1
    branch, path, _ = jobs[choice]

    action = input("1. Accept\n2. Open VSCode\n3. Redo\nChoice: ")
    {"1": lambda: accept_job(branch), "2": lambda: subprocess.run(["code", path]), "3": lambda: redo_job(branch)}.get(action, lambda: None)()

def accept_job(branch):
    aios_db.execute("autollm", "UPDATE worktrees SET status='done' WHERE branch=?", (branch,))
    job_id = aios_db.query("autollm", "SELECT job_id FROM worktrees WHERE branch=?", (branch,))[0][0]
    aios_db.execute("jobs", "UPDATE jobs SET status='done' WHERE id=?", (job_id,))

def redo_job(branch):
    worktree = aios_db.query("autollm", "SELECT path FROM worktrees WHERE branch=?", (branch,))[0]
    task = input("New task: ")
    launch_job({"branch": branch, "path": worktree[0]}, task)

def clean():
    worktrees = aios_db.query("autollm", "SELECT branch, path FROM worktrees WHERE status='done'")
    list(map(lambda w: subprocess.run(["git", "worktree", "remove", w[0]], cwd=repo_path), worktrees))
    aios_db.execute("autollm", "DELETE FROM worktrees WHERE status='done'")
    print(f"Cleaned {len(worktrees)} worktrees")

def models():
    models_list = aios_db.query("autollm", "SELECT name, params FROM models")
    list(map(lambda m: print(f"{m[0]}: {m[1]}"), models_list))

def add_model():
    name = sys.argv[2]
    params = " ".join(sys.argv[3:])
    aios_db.execute("autollm", "INSERT INTO models(name, params) VALUES (?, ?)", (name, params))

def status():
    running = len(aios_db.query("autollm", "SELECT id FROM worktrees WHERE status='running'"))
    review = len(aios_db.query("autollm", "SELECT id FROM worktrees WHERE status='review'"))
    done = len(aios_db.query("autollm", "SELECT id FROM worktrees WHERE status='done'"))
    print(f"Running: {running}, Review: {review}, Done: {done}")

actions = {
    "run": run,
    "terminal": terminal,
    "terminal_branch": lambda: terminal_branch(),
    "review": review,
    "clean": clean,
    "models": models,
    "add_model": add_model,
    "status": status
}

aios_db.execute("jobs", "CREATE TABLE IF NOT EXISTS jobs(id INTEGER PRIMARY KEY, name TEXT, status TEXT, output TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")
actions.get(command, run)()