#!/usr/bin/env python3
"""a snap — snapshot the OPEN tmux windows and restore them after a reboot.

  a snap            save: capture every window of the tmux session → snapshot
  a snap show       print the saved snapshot
  a snap restore    recreate each window in its cwd, replaying its command
                    (add --dry to print the tmux commands without running them)

Generic: each window is saved as (name, cwd, command) and replayed with `tmux new-window`.
The command is the window's start command, EXCEPT claude windows, whose real resumable session
is resolved to `claude --resume <id>` — Claude compacts a long session to a new on-disk id, so
the launch --session-id is stale, and a fresh window's start command holds /tmp refs that vanish
on reboot. Distinct claude panes claim distinct transcripts. Restore is triggered on tmux
session-create (a's tm_ensure_sess), per-device from a local snapshot, so a reboot/RAM-kill is
recoverable. Set A_SNAP_SESSION to target a tmux session other than `a`.

TODO: claude is the first per-program resolver; add more (e.g. resume-able REPLs) as needed.
"""
import sys, os, json, glob, re, socket, subprocess

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
SNAPDIR = os.path.expanduser("~/a/adata/git/sessions")
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")
ID = re.compile(r"--(?:resume|session-id)[ =]+([0-9a-f-]{36})")   # the session id on a claude cmdline
RESUME = "claude --dangerously-skip-permissions --resume %s; exec bash"


def tree(pid):                                        # pid + all descendants
    seen, i = [str(pid)], 0
    while i < len(seen):
        try: kids = open(f"/proc/{seen[i]}/task/{seen[i]}/children").read().split()
        except OSError: kids = []
        for c in kids:
            if c not in seen: seen.append(c)
        i += 1
    return seen


def claude_id(pid):                                   # claude cmdline session id under this window; "" if claude w/o id; None if not claude
    for p in tree(pid):
        try: cl = open(f"/proc/{p}/cmdline", "rb").read().decode("utf-8", "replace")
        except OSError: continue
        if "claude" in cl:
            m = ID.search(cl)
            return m.group(1) if m else ""
    return None


def have(sid): return bool(sid) and bool(glob.glob(f"{PROJ}/*/{sid}.jsonl"))


def newest_in(cwd, skip):                             # newest claude transcript in cwd's project dir not already owned
    d = PROJ + "/" + "".join(c if c.isalnum() else "-" for c in cwd)
    for j in sorted(glob.glob(f"{d}/*.jsonl"), key=os.path.getmtime, reverse=True):
        s = os.path.basename(j)[:-6]
        if s not in skip: return s
    return None


def windows():                                        # per window: [name, cwd, active-pane pid, start command]
    r = subprocess.run(["tmux", "list-windows", "-t", TMS, "-F",
                        "#{window_name}\t#{pane_current_path}\t#{pane_pid}\t#{pane_start_command}"],
                       capture_output=True, text=True)
    return [(w + [""] * 4)[:4] for w in (l.split("\t", 3) for l in r.stdout.splitlines() if l)] if not r.returncode else []


def save():
    info = [(n, cwd, claude_id(pid), sc) for n, cwd, pid, sc in windows()]
    claimed = {cid for _n, _c, cid, _s in info if have(cid)}        # claude ids that resolve to a transcript
    used, jobs = set(), []
    for name, cwd, cid, sc in info:
        if "while a i" in sc: continue                             # skip a's session-keeper window
        if cid is not None:                                        # claude window → resume its real session
            sid = cid if have(cid) else newest_in(cwd, claimed | used)
            if not sid or sid in used: continue
            used.add(sid); cmd = RESUME % sid
        else:                                                      # any other program → replay its start command (shell if none)
            cmd = sc.strip()
        jobs.append({"window": name, "cwd": cwd, "cmd": cmd})
    os.makedirs(SNAPDIR, exist_ok=True)
    json.dump({"host": DEV, "session": TMS, "jobs": jobs}, open(SNAP, "w"), indent=1)
    print(f"✓ snapshot {len(jobs)} window(s) → {SNAP}")
    for j in jobs: print(f"  {j['window']:24} {j['cmd'][:54] or '(shell)'}")
    return jobs


def show():
    if not os.path.exists(SNAP):
        print("(no snapshot)"); return
    d = json.load(open(SNAP))
    print(f"{d['host']} [{d.get('session', '?')}]: {len(d['jobs'])} window(s)")
    for j in d["jobs"]:
        print(f"  {j['window']:24} {j['cmd'][:54] or '(shell)'}  {j['cwd']}")


def run(argv, dry):
    print("  $ " + " ".join(argv)) if dry else subprocess.run(argv, check=False)


def restore(dry=False):
    if not os.path.exists(SNAP):
        print("(no snapshot to restore)"); return
    jobs = json.load(open(SNAP))["jobs"]
    if subprocess.run(["tmux", "has-session", "-t", TMS], capture_output=True).returncode:
        run(["tmux", "new-session", "-d", "-s", TMS], dry)
    for j in jobs:
        argv = ["tmux", "new-window", "-d", "-t", TMS, "-n", j["window"], "-c", j["cwd"]]
        if j["cmd"]: argv.append(j["cmd"])
        run(argv, dry)
        print(f"  {'[dry] ' if dry else ''}↻ {j['window']:24} {j['cmd'][:50] or '(shell)'}")
    print(f"{'[dry] ' if dry else ''}✓ restored {len(jobs)} window(s) into tmux '{TMS}'")


def main(a):
    if a and a[0] == "snap": a = a[1:]                # `a snap …` forwards the tool name; drop it
    cmd = a[0] if a else "save"
    if cmd in ("save", "s"): save()
    elif cmd == "show": show()
    elif cmd == "restore": restore(dry=("--dry" in a or "-n" in a))
    else: print("usage: a snap [save|show|restore [--dry]]")


if __name__ == "__main__":
    main(sys.argv[1:])
