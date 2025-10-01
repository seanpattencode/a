#!/usr/bin/env python3
import sys, subprocess, json
sys.path.append("/home/seanpatten/projects/AIOS/core")
import aios_db
from pathlib import Path
command = (sys.argv + ["run"])[1]
arg1, arg2, arg3 = (sys.argv + ["", "", "", ""])[2:5]
def run():
    subprocess.run(arg1, shell=True, timeout=5, cwd=arg2 or ".")
def translate():
    cmds = {"claude": f'claude --dangerously-skip-permissions "{arg2}"', "codex": f'codex -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox "{arg2}"'}
    result = subprocess.run(cmds.get(arg1, f'echo "{arg2}"'), shell=True, capture_output=True, text=True, timeout=999999, cwd=arg3 or ".")
    print(result.stdout)
    sys.stderr.write(result.stderr)
def execute():
    result = subprocess.run(arg1, shell=True, capture_output=True, text=True, timeout=999999, cwd=arg2 or ".")
    print(result.stdout)
    sys.stderr.write(result.stderr)
def branch():
    repo = Path(arg2)
    branch_name = f"worktree-{arg1[:20].replace(' ', '-').replace('/', '-')}"
    worktree_path = repo.parent / f"{repo.name}-{branch_name}"
    result = subprocess.run(["git", "worktree", "add", "-b", branch_name, str(worktree_path)], cwd=str(repo), capture_output=True, text=True, timeout=5)
    print(f"Created: {worktree_path}\n{result.stdout}")
    sys.stderr.write(result.stderr)
def save():
    workflows = aios_db.read("workflows")
    workflows[arg1] = json.loads(arg2)
    aios_db.write("workflows", workflows)
    print(f"Saved: {arg1}")
def load():
    print(json.dumps(aios_db.read("workflows").get(arg1, {})))
def gitpush():
    subprocess.run(["git", "add", "."], cwd=arg1, timeout=5)
    subprocess.run(["git", "commit", "-m", "worktree update"], cwd=arg1, timeout=5, capture_output=True)
    subprocess.run(["git", "push"], cwd=arg1, timeout=5, capture_output=True)
    print("Pushed changes")
def terminal():
    subprocess.Popen(["gnome-terminal", "--working-directory", arg1])
    print(f"Terminal: {arg1}")
def vscode():
    subprocess.Popen(["code", arg1])
    print(f"VSCode: {arg1}")
{"run": run, "translate": translate, "execute": execute, "branch": branch, "save": save, "load": load, "gitpush": gitpush, "terminal": terminal, "vscode": vscode}.get(command, run)()
