#!/usr/bin/env python3
"""a snap — snapshot the OPEN tmux claude sessions and restore them after a reboot.

  a snap            save: capture every open tmux pane's LIVE claude session id → snapshot
  a snap show       print the saved snapshot
  a snap restore    recreate each window in its cwd: claude --dangerously-skip-permissions --resume <sid>
                    (add --dry to print the tmux commands without running them)

Unlike `a resume` (which lists *recent* ~/.claude jsonls — and warns they get buried by
scanner runs), this captures the sessions ACTUALLY OPEN right now: for each tmux pane it walks
the process tree and reads the real `--session-id <uuid>` out of the claude process's
/proc/<pid>/cmdline. That exact set is what gets restored — the precondition for safe daily
auto-reboot (see i/productivity-loss.md #1). Snapshot is per-device and on local disk, so a
reboot or RAM-kill is recoverable. Set A_SNAP_SESSION to target a tmux session other than `a`.
"""
import sys, os, re, json, glob, socket, subprocess

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
SNAPDIR = os.path.expanduser("~/a/adata/git/sessions")
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")
UUID = re.compile(r"--session-id[= ]+([0-9a-fA-F-]{36})")


def cmdline(pid):
    try:
        return open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ").decode("utf-8", "replace")
    except OSError:
        return ""


def kids(pid):
    try:                                              # fast path: thread children file
        return open(f"/proc/{pid}/task/{pid}/children").read().split()
    except OSError:
        out = []                                      # fallback: scan PPid
        for d in os.listdir("/proc"):
            if not d.isdigit():
                continue
            try:
                for ln in open(f"/proc/{d}/status"):
                    if ln.startswith("PPid:"):
                        if ln.split()[1] == str(pid):
                            out.append(d)
                        break
            except OSError:
                pass
        return out


def tree(pid):                                        # pid + all descendants
    seen, i = [str(pid)], 0
    while i < len(seen):
        for c in kids(seen[i]):
            if c not in seen:
                seen.append(c)
        i += 1
    return seen


def sid_of(pane_pid):                                 # live claude session id under this pane, or None
    for p in tree(pane_pid):
        cl = cmdline(p)
        if "claude" in cl:
            m = UUID.search(cl)
            if m:
                return m.group(1).lower()
    return None


def panes():
    fmt = "#{window_index}\t#{window_name}\t#{pane_pid}\t#{pane_current_path}"
    r = subprocess.run(["tmux", "list-panes", "-s", "-t", TMS, "-F", fmt], capture_output=True, text=True)
    if r.returncode:
        return []
    return [l.split("\t") for l in r.stdout.splitlines() if l]


def jsonl_exists(sid):
    return bool(glob.glob(f"{PROJ}/*/{sid}.jsonl"))   # glob avoids guessing claude's cwd-encoding


def save():
    seen, jobs = set(), []
    for _win, name, pid, cwd in panes():
        sid = sid_of(pid)
        key = (name, cwd, sid)
        if key in seen:                               # dedupe grouped-session mirrors (a / a-NNNN)
            continue
        seen.add(key)
        if sid:                                       # only claude windows carry resumable state
            jobs.append({"window": name, "cwd": cwd, "sid": sid})
    os.makedirs(SNAPDIR, exist_ok=True)
    json.dump({"host": DEV, "session": TMS, "jobs": jobs}, open(SNAP, "w"), indent=1)
    print(f"✓ snapshot {len(jobs)} claude session(s) → {SNAP}")
    for j in jobs:
        print(f"  {j['window']:24} {j['sid']}  {j['cwd']}")
    return jobs


def show():
    if not os.path.exists(SNAP):
        print("(no snapshot)")
        return
    d = json.load(open(SNAP))
    print(f"{d['host']} [{d.get('session', '?')}]: {len(d['jobs'])} session(s)")
    for j in d["jobs"]:
        print(f"  {j['window']:24} {j['sid']}  {j['cwd']}")


def run(argv, dry):
    if dry:
        print("  $ " + " ".join(argv))
        return
    subprocess.run(argv, check=False)


def restore(dry=False):
    if not os.path.exists(SNAP):
        print("(no snapshot to restore)")
        return
    jobs = json.load(open(SNAP))["jobs"]
    if subprocess.run(["tmux", "has-session", "-t", TMS], capture_output=True).returncode:
        run(["tmux", "new-session", "-d", "-s", TMS], dry)
    n = 0
    for j in jobs:
        sid, cwd, name = j["sid"], j["cwd"], j["window"]
        if not dry and not jsonl_exists(sid):
            print(f"  ! skip {name}: session {sid} jsonl not found")
            continue
        cmd = f"claude --dangerously-skip-permissions --resume {sid}; exec bash"
        run(["tmux", "new-window", "-d", "-t", TMS, "-n", name, "-c", cwd, cmd], dry)
        print(f"  {'[dry] ' if dry else ''}↻ {name}  {sid}  ({cwd})")
        n += 1
    print(f"{'[dry] ' if dry else ''}✓ restored {n} window(s) into tmux '{TMS}'")


def main(a):
    if a and a[0] == "snap":                          # `a snap …` forwards the tool name; drop it
        a = a[1:]
    cmd = a[0] if a else "save"
    if cmd in ("save", "s"):
        save()
    elif cmd == "show":
        show()
    elif cmd == "restore":
        restore(dry=("--dry" in a or "-n" in a))
    else:
        print("usage: a snap [save|show|restore [--dry]]")


if __name__ == "__main__":
    main(sys.argv[1:])
