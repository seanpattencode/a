#!/usr/bin/env python3
import sys, json, subprocess
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db
from pathlib import Path
from datetime import datetime

cmd = (sys.argv + ["list"])[1]

def get_workflows():
    try:
        return aios_db.read("workflows")
    except:
        aios_db.write("workflows", {})
        return {}

def get_nodes():
    try:
        return aios_db.read("workflow_nodes")
    except:
        aios_db.write("workflow_nodes", [])
        return []

def add_node():
    nodes = get_nodes()
    col = int((sys.argv + ["", "0"])[2])
    args = sys.argv[3:]
    folder = str(Path.cwd())
    text = " ".join(args)
    for i, arg in enumerate(args):
        if arg.startswith("/") and Path(arg).exists():
            folder = arg
            text = " ".join(args[:i] + args[i+1:])
            break
    node = {"id": len(nodes), "col": col, "text": text, "folder": folder, "parent": None, "children": [], "branch": None, "created": datetime.now().isoformat()}
    aios_db.write("workflow_nodes", nodes + [node])
    print(f"{node['id']}")

def list_nodes():
    cols = {}
    for n in get_nodes():
        cols.setdefault(n["col"], []).append(n)
    for c in sorted(cols.keys()):
        print(f"Column {c}:")
        list(map(lambda n: print(f"  {n['id']}: {n['text'][:50]}"), cols[c]))

def expand():
    nid = int((sys.argv + ["", "0"])[2])
    instruction = " ".join(sys.argv[3:])
    nodes = get_nodes()
    parent = nodes[nid]
    new_text = f"{parent['text']}\n{instruction}" if instruction else parent['text']
    child = {"id": len(nodes), "col": parent["col"] + 1, "text": new_text, "folder": parent["folder"], "parent": nid, "children": [], "branch": None, "created": datetime.now().isoformat()}
    nodes[nid]["children"].append(child["id"])
    aios_db.write("workflow_nodes", nodes + [child])
    print(f"{child['id']}")

def branch():
    nid = int((sys.argv + ["", "0"])[2])
    btype = (sys.argv + ["", "", "folder"])[3]
    nodes = get_nodes()
    node = nodes[nid]
    folder = node["folder"]
    if btype == "worktree":
        branch_name = f"workflow-{nid}-{datetime.now():%Y%m%d%H%M%S}"
        path = f"{folder}-{branch_name}"
        subprocess.run(["git", "worktree", "add", "-b", branch_name, path], cwd=folder, capture_output=True, timeout=5)
        nodes[nid]["branch"] = {"type": "worktree", "path": path, "branch": branch_name}
    elif btype == "subfolder":
        path = f"{folder}/workflow-{nid}"
        Path(path).mkdir(parents=True, exist_ok=True)
        nodes[nid]["branch"] = {"type": "subfolder", "path": path}
    else:
        nodes[nid]["branch"] = {"type": "main", "path": folder}
    aios_db.write("workflow_nodes", nodes)
    print(nodes[nid]["branch"]["path"])

def execute():
    nid = int((sys.argv + ["", "0"])[2])
    nodes = get_nodes()
    node = nodes[nid]
    branch = node.get("branch") or {}
    path = branch.get("path") if branch else node["folder"]
    full_prompt = node["text"]
    parent_id = node.get("parent")
    while parent_id is not None:
        parent = nodes[parent_id]
        full_prompt = f"{parent['text']}\n{full_prompt}"
        parent_id = parent.get("parent")
    aios_db.execute("jobs", "INSERT INTO jobs(name, status, output) VALUES (?, 'running', ?)", (f"workflow-{nid}", node["text"]))
    job_id = aios_db.query("jobs", "SELECT MAX(id) FROM jobs")[0][0]
    subprocess.Popen(["claude", "--dangerously-skip-permissions", full_prompt], cwd=path, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"{job_id}")

def save_workflow():
    name = (sys.argv + ["", "default"])[2]
    workflows = get_workflows()
    workflows[name] = {"nodes": get_nodes(), "saved": datetime.now().isoformat()}
    aios_db.write("workflows", workflows)
    print(f"Saved: {name}")

def load_workflow():
    name = (sys.argv + ["", "default"])[2]
    workflows = get_workflows()
    aios_db.write("workflow_nodes", workflows.get(name, {}).get("nodes", []))
    print(f"Loaded: {name}")

def git_push():
    nid = int((sys.argv + ["", "0"])[2])
    msg = " ".join(sys.argv[3:]) or "workflow update"
    nodes = get_nodes()
    node = nodes[nid]
    branch = node.get("branch") or {}
    path = branch.get("path") if branch else node["folder"]
    subprocess.run(["git", "add", "."], cwd=path, timeout=5)
    subprocess.run(["git", "commit", "-m", msg], cwd=path, timeout=5, capture_output=True)
    result = subprocess.run(["git", "push"], cwd=path, timeout=5, capture_output=True, text=True)
    if "no upstream branch" in result.stderr:
        branch_name = subprocess.run(["git", "branch", "--show-current"], cwd=path, capture_output=True, text=True, timeout=5).stdout.strip()
        subprocess.run(["git", "push", "--set-upstream", "origin", branch_name], cwd=path, timeout=5)
    print(f"Pushed: {path}")

def terminal():
    nid = int((sys.argv + ["", "0"])[2])
    term = (sys.argv + ["", "", "gnome-terminal"])[3]
    nodes = get_nodes()
    node = nodes[nid]
    branch = node.get("branch") or {}
    path = branch.get("path") if branch else node["folder"]
    subprocess.Popen([term, "--working-directory", path])
    print(f"Terminal: {path}")

def comment():
    nid = int((sys.argv + ["", "0"])[2])
    text = " ".join(sys.argv[3:])
    nodes = get_nodes()
    nodes[nid].setdefault("comments", []).append({"text": text, "time": datetime.now().isoformat(), "author": "human"})
    aios_db.write("workflow_nodes", nodes)
    print(f"Comment added to {nid}")

{"add": add_node, "list": list_nodes, "expand": expand, "branch": branch, "exec": execute, "save": save_workflow, "load": load_workflow, "push": git_push, "term": terminal, "comment": comment}.get(cmd, list_nodes)()
