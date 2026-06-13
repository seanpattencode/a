#!/usr/bin/env python3
"""a snap — save/restore the OPEN tmux windows across a reboot.

  a snap [save] | show | restore [--dry]

Each window → (name, cwd, cmd), replayed with tmux new-window. claude windows resume their real
transcript (the launch id goes stale on compaction); `a ssh` host windows reconnect; all others
reopen as a shell in cwd. Dup names are real — each open window absorbs one job, the rest recreate.
Restore fires on session-create (tm_ensure_sess). A_SNAP_SESSION overrides the session.
"""
import sys, os, json, glob, re, socket, subprocess

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
SNAPDIR = os.path.expanduser("~/a/adata/git/sessions")
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")
ID = re.compile(r"--(?:resume|session-id)[ =]+([0-9a-f-]{36})")   # session id on a claude cmdline
RESUME = "claude --dangerously-skip-permissions --resume %s; exec bash"
HOST = os.path.expanduser("~/a/adata/git/ssh/%s.txt")             # a ssh host registry


LINUX = os.path.isdir("/proc")
PS = {}                                               # macOS/BSD: {pid: (ppid, command)} from one `ps`


def _scan():                                          # populate PS on platforms without /proc (macOS)
    global PS
    out = subprocess.run(["ps", "-axww", "-o", "pid=,ppid=,command="], capture_output=True, text=True).stdout
    PS = {}
    for ln in out.splitlines():
        p = ln.split(None, 2)
        if len(p) >= 2: PS[p[0]] = (p[1], p[2] if len(p) > 2 else "")


def _children(pid):                                   # direct children of pid — /proc on Linux, ps map elsewhere
    if LINUX:
        try: return open(f"/proc/{pid}/task/{pid}/children").read().split()
        except OSError: return []
    return [p for p, (pp, _c) in PS.items() if pp == pid]


def _cmdline(pid):                                    # full command line of pid
    if LINUX:
        try: return open(f"/proc/{pid}/cmdline", "rb").read().decode("utf-8", "replace")
        except OSError: return ""
    return PS.get(str(pid), ("", ""))[1]


def tree(pid):                                        # pid + all descendants
    seen, i = [str(pid)], 0
    while i < len(seen):
        for c in _children(seen[i]):
            if c not in seen: seen.append(c)
        i += 1
    return seen


def claude_id(pids):                                  # claude session id under these panes; "" if claude w/o id; None if not claude
    for p in (t for pid in pids for t in tree(pid)):
        cl = _cmdline(p)
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


def windows():                                        # [name, cwd, pane pids, start cmd] — all panes (claude may be off the active pane after `a done` splits)
    r = subprocess.run(["tmux", "list-panes", "-s", "-t", TMS, "-F",
                        "#{window_id}\t#{window_name}\t#{pane_current_path}\t#{pane_pid}\t#{pane_start_command}"],
                       capture_output=True, text=True)
    w = {}
    for l in r.stdout.splitlines():
        i, n, cwd, pid, sc = (l.split("\t", 4) + [""] * 5)[:5]
        w.setdefault(i, [n, cwd, [], sc])[2].append(pid)
    return list(w.values())


def save():
    if not LINUX: _scan()                             # macOS: snapshot the process table once
    info = [(n, cwd, claude_id(pid), sc) for n, cwd, pid, sc in windows()]
    if not info: print("x no windows — snapshot kept"); return []  # don't clobber with emptiness
    claimed = {cid for _n, _c, cid, _s in info if have(cid)}        # ids that resolve to a transcript
    used, jobs = set(), []
    for name, cwd, cid, sc in info:
        if "while a i" in sc: continue                             # skip a's session-keeper window
        cmd = ""                                                   # unresolvable → shell
        if cid is not None:                                        # claude window → resume its session
            sid = cid if have(cid) else newest_in(cwd, claimed | used)
            if sid and sid not in used: used.add(sid); cmd = RESUME % sid
        elif os.path.exists(HOST % name): cmd = "a ssh %s; exec bash" % name   # ssh window → reconnect
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


def restore(dry=False):
    if not os.path.exists(SNAP):
        print("(no snapshot to restore)"); return
    jobs = json.load(open(SNAP))["jobs"]
    fresh = subprocess.run(["tmux", "has-session", "-t", TMS], capture_output=True).returncode != 0
    seen = [] if fresh else subprocess.run(["tmux", "list-windows", "-t", TMS, "-F", "#{window_name}"],
                          capture_output=True, text=True).stdout.split()          # idempotent: skip open windows
    n = 0
    for j in jobs:
        if j["window"] in seen: seen.remove(j["window"]); continue  # absorb one open window per name; dups still restore
        cwd = j["cwd"] if os.path.isdir(j["cwd"]) else os.path.expanduser("~")  # cwd may be gone
        verb = ["new-session", "-d", "-s", TMS] if fresh and not n else ["new-window", "-d", "-t", TMS + ":"]  # ':' = session target (bare name can hit a like-named window)
        argv = ["tmux"] + verb + ["-n", j["window"], "-c", cwd] + ([j["cmd"]] if j["cmd"] else [])
        print("  $ " + " ".join(argv)) if dry else subprocess.run(argv, check=False); n += 1
        print(f"  {'[dry] ' if dry else ''}↻ {j['window']:24} {j['cmd'][:50] or '(shell)'}")
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
