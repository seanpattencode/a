#!/usr/bin/env python3
"""a snap — snapshot the OPEN tmux claude sessions and restore them after a reboot.

  a snap            save: capture every open tmux pane's LIVE claude session → snapshot
  a snap show       print the saved snapshot
  a snap restore    recreate each window in its cwd: claude --dangerously-skip-permissions --resume <sid>
                    (add --dry to print the tmux commands without running them)

For each tmux pane it walks the process tree to the claude proc and records the REAL resumable
session: the newest transcript jsonl in that proc's cwd project dir. (The launch `--session-id`
on the cmdline is NOT reliable — Claude compacts a long session to a new on-disk id, so resuming
the launch id errors.) Snapshot is per-device on local disk, so a reboot/RAM-kill is recoverable.
Set A_SNAP_SESSION to target a tmux session other than `a`.
"""
import sys, os, json, glob, socket, subprocess

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
SNAPDIR = os.path.expanduser("~/a/adata/git/sessions")
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")


def tree(pid):                                        # pid + all descendants
    seen, i = [str(pid)], 0
    while i < len(seen):
        try: kids = open(f"/proc/{seen[i]}/task/{seen[i]}/children").read().split()
        except OSError: kids = []
        for c in kids:
            if c not in seen: seen.append(c)
        i += 1
    return seen


def sid_of(pane_pid):                                 # real resumable session: newest transcript in the claude proc's cwd
    for p in tree(pane_pid):
        try:
            if b"claude" not in open(f"/proc/{p}/cmdline", "rb").read(): continue
            cwd = os.readlink(f"/proc/{p}/cwd")
        except OSError:
            continue
        d = PROJ + "/" + "".join(c if c.isalnum() else "-" for c in cwd)
        js = sorted(glob.glob(f"{d}/*.jsonl"), key=os.path.getmtime)
        if js: return os.path.basename(js[-1])[:-6]
    return None


def panes():
    fmt = "#{window_index}\t#{window_name}\t#{pane_pid}\t#{pane_current_path}"
    r = subprocess.run(["tmux", "list-panes", "-s", "-t", TMS, "-F", fmt], capture_output=True, text=True)
    return [l.split("\t") for l in r.stdout.splitlines() if l] if not r.returncode else []


def save():
    seen, jobs = set(), []
    for _win, name, pid, cwd in panes():
        sid = sid_of(pid)
        key = (name, cwd, sid)
        if sid and key not in seen:                   # only claude windows resume; dedupe grouped-session mirrors
            seen.add(key)
            jobs.append({"window": name, "cwd": cwd, "sid": sid})
    os.makedirs(SNAPDIR, exist_ok=True)
    json.dump({"host": DEV, "session": TMS, "jobs": jobs}, open(SNAP, "w"), indent=1)
    print(f"✓ snapshot {len(jobs)} claude session(s) → {SNAP}")
    for j in jobs:
        print(f"  {j['window']:24} {j['sid']}  {j['cwd']}")
    return jobs


def show():
    if not os.path.exists(SNAP):
        print("(no snapshot)"); return
    d = json.load(open(SNAP))
    print(f"{d['host']} [{d.get('session', '?')}]: {len(d['jobs'])} session(s)")
    for j in d["jobs"]:
        print(f"  {j['window']:24} {j['sid']}  {j['cwd']}")


def run(argv, dry):
    print("  $ " + " ".join(argv)) if dry else subprocess.run(argv, check=False)


def restore(dry=False):
    if not os.path.exists(SNAP):
        print("(no snapshot to restore)"); return
    jobs = json.load(open(SNAP))["jobs"]
    if subprocess.run(["tmux", "has-session", "-t", TMS], capture_output=True).returncode:
        run(["tmux", "new-session", "-d", "-s", TMS], dry)
    n = 0
    for j in jobs:
        sid, cwd, name = j["sid"], j["cwd"], j["window"]
        if not dry and not glob.glob(f"{PROJ}/*/{sid}.jsonl"):
            print(f"  ! skip {name}: session {sid} jsonl not found"); continue
        cmd = f"claude --dangerously-skip-permissions --resume {sid}; exec bash"
        run(["tmux", "new-window", "-d", "-t", TMS, "-n", name, "-c", cwd, cmd], dry)
        print(f"  {'[dry] ' if dry else ''}↻ {name}  {sid}  ({cwd})")
        n += 1
    print(f"{'[dry] ' if dry else ''}✓ restored {n} window(s) into tmux '{TMS}'")


def main(a):
    if a and a[0] == "snap": a = a[1:]                # `a snap …` forwards the tool name; drop it
    cmd = a[0] if a else "save"
    if cmd in ("save", "s"): save()
    elif cmd == "show": show()
    elif cmd == "restore": restore(dry=("--dry" in a or "-n" in a))
    else: print("usage: a snap [save|show|restore [--dry]]")


if __name__ == "__main__":
    main(sys.argv[1:])
