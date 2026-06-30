#!/usr/bin/env python3
"""a res — resume agents: interactive pick, or save/restore the OPEN tmux windows across a reboot.
(same command as `a resume` and `a snap`)

  a res                                  interactive: pick a recent claude session (c/g = codex/gemini native resume)
  a res save | show | restore [--dry]    snapshot / restore the tmux session across a reboot

Reboot revival: each window → (name, cwd, cmd). claude/codex/gemini windows resume their session
(claude's launch id goes stale on compaction); `a ssh` host windows reconnect; all others reopen as a
shell in cwd. Restore fires on session-create (tm_ensure_sess). A_SNAP_SESSION overrides the session.
"""
import sys, os, json, glob, re, socket, subprocess, time, termios, tty, select

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
SNAPDIR = os.path.expanduser("~/a/adata/git/sessions")
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")
ID = re.compile(r"--(?:resume|session-id)[ =]+([0-9a-f-]{36})")   # session id on a claude cmdline
RESUME = {"claude": "claude --dangerously-skip-permissions --resume %s; exec bash",  # %s = session id
          "codex": "codex resume --last; exec bash", "gemini": "gemini --yolo --resume latest; exec bash"}
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


AEXE = {"claude", "codex", "gemini"}                  # agent binaries we revive natively

def agent(pids):                                      # (kind, sid) of the agent under these panes; (None, "") if none
    for p in (t for pid in pids for t in tree(pid)):  # match the LAUNCHED BINARY, never a prompt substring:
        cl = _cmdline(p)                              # every agent carries the tool-list in argv, so "x in cl" false-matches
        for tk in cl.split("\0")[:2]:                 # argv[0:2] = exe (+ `node <script>`); the prompt sits later
            b = os.path.basename(tk)
            if b in AEXE:
                if b == "claude": m = ID.search(cl); return ("claude", m.group(1) if m else "")
                return (b, "")
    return (None, "")


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
    info = [(n, cwd, *agent(pid), sc) for n, cwd, pid, sc in windows()]   # (name, cwd, kind, sid, sc)
    if not info: print("x no windows — snapshot kept"); return []  # don't clobber with emptiness
    claimed = {sid for _n, _c, k, sid, _s in info if k == "claude" and have(sid)}  # claude ids with a transcript
    used, jobs = set(), []
    for name, cwd, kind, sid, sc in info:
        if "while a i" in sc: continue                             # skip a's session-keeper window
        cmd = s = ""                                               # unknown window → shell
        if kind == "claude":                                       # claude → resume its real transcript
            s = sid if have(sid) else newest_in(cwd, claimed | used)
            if s and s not in used: used.add(s); cmd = RESUME["claude"] % s
        elif kind in ("codex", "gemini"): cmd = RESUME[kind]       # codex/gemini → native resume
        elif os.path.exists(HOST % name): cmd = "a ssh %s; exec bash" % name   # ssh window → reconnect
        jobs.append({"window": name, "cwd": cwd, "cmd": cmd, "preview": _preview(s) if s else ""})  # bake tail → syncs cross-device
    os.makedirs(SNAPDIR, exist_ok=True)
    json.dump({"host": DEV, "session": TMS, "jobs": jobs}, open(SNAP, "w"), indent=1)
    print(f"✓ snapshot {len(jobs)} window(s) → {SNAP}")
    for j in jobs: print(f"  {j['window']:24} {j['cmd'][:54] or '(shell)'}")
    return jobs


def _preview(sid):                                   # tail (last 64KB) of a claude transcript → last human line = "what it's about"
    for fp in glob.glob(f"{PROJ}/*/{sid}*.jsonl"):
        try:
            with open(fp, "rb") as fh:
                fh.seek(0, 2); fh.seek(max(0, fh.tell() - 65536)); txt = fh.read().decode("utf-8", "ignore")
        except OSError: continue
        m = [x for x in re.findall(r'"role":"user","content":"([^"]{2,90})', txt)
             if "command-name" not in x and "local-command" not in x and not x.startswith(("<", "[Request"))]
        if m: return re.sub(r"\s+", " ", m[-1])[:60]
    return ""


def _live():                                         # names of currently-open local tmux windows (● = live)
    r = subprocess.run(["tmux", "list-windows", "-t", TMS, "-F", "#{window_name}"], capture_output=True, text=True)
    return set(r.stdout.split())


def show(flt=""):                                     # aggregate EVERY device's saved windows — snapshots sync via git
    files = sorted(glob.glob(f"{SNAPDIR}/*.json"), key=os.path.getmtime, reverse=True)
    if not files: print("(no snapshots — run `a res save` on a device)"); return
    live, n = _live(), 0
    for f in files:
        try: d = json.load(open(f))
        except (OSError, ValueError): continue
        host, here = d.get("host", os.path.basename(f)[:-5]), d.get("host") == DEV
        rows = [j for j in d["jobs"] if not flt or flt.lower() in (j["window"] + j["cmd"] + j["cwd"] + j.get("preview", "")).lower()]
        if not rows: continue
        print(f"\n\033[1m{host}\033[0m{' ·here' if here else ''}  {len(rows)} win  ({_age(time.time() - os.path.getmtime(f))} ago)")
        for j in rows:
            sid = ID.search(j["cmd"])
            mark = "●" if here and j["window"] in live else "⏸"
            desc = (_preview(sid.group(1)) if here and sid else "") or j.get("preview", "") \
                or (("claude " + sid.group(1)[:7]) if sid else (j["cmd"][:44] or "(shell)"))   # what it's about
            print(f"  {mark} {j['window']:15.15} {os.path.basename(j['cwd']):<8} {desc[:58]}"); n += 1
    print(f"\n{n} window(s) · {len(files)} device(s){'  /'+flt if flt else '   search: a res show <text>'}")


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


def _age(s):
    s = int(s); return f"{s//60}m" if s < 3600 else (f"{s//3600}h" if s < 86400 else f"{s//86400}d")


def _claude_list(flt="", mins=0):                     # recent claude transcripts: [(mtime, id, cwd, turns, preview)]
    import time; now, out = time.time(), []
    for f in sorted(glob.glob(f"{PROJ}/*/*.jsonl"), key=os.path.getmtime, reverse=True)[:150]:
        if mins and os.path.getmtime(f) < now - mins * 60: continue
        try: txt = open(f, errors="ignore").read()
        except OSError: continue
        if flt and flt.lower() not in txt.lower(): continue
        turns = txt.count('"role":"user","content":"')
        if turns < 2: continue
        m = re.search(r'"cwd":"([^"]+)"', txt); cwd = m.group(1) if m else ""
        if not os.path.isdir(cwd): continue
        p = re.search(r'"role":"user","content":"([^"]{1,50})', txt)
        out.append((os.path.getmtime(f), os.path.basename(f)[:-6], cwd, turns, re.sub(r"\s+", " ", p.group(1)) if p else ""))
        if len(out) >= 20: break
    return now, out


def _claunch(sid, cwd):                               # resume one claude transcript in a new window
    subprocess.run(["tmux", "new-window", "-n", f"r-{os.path.basename(cwd)}", "-c", cwd, RESUME["claude"] % sid])


def pick():                                           # interactive resume picker (claude inline; codex/gemini native)
    if not os.environ.get("TMUX"): print("x need tmux"); return
    flt = ""
    while True:
        now, L = _claude_list(flt)
        print()
        for i, (mt, sid, cwd, turns, prev) in enumerate(L, 1):
            print(f"{i:2d}) {_age(now - mt):>4} {turns:3d}t {os.path.basename(cwd):<12} {prev}")
        if not L: print("(none)")
        print(" c) codex   g) gemini   (native resume)")
        try: x = input(f"# {('[/' + flt + '] ') if flt else ''}pick #, c/g, text=search, a=restore recent, q: ").strip()
        except EOFError: return
        if x in ("", "q"): return
        if x == "c": subprocess.run(["tmux", "new-window", "-n", "r-codex", "codex resume; exec bash"]); return
        if x == "g": subprocess.run(["tmux", "new-window", "-n", "r-gemini", "gemini --yolo --resume latest; exec bash"]); return
        if x == "a":
            try: mins = int(input("restore all claude active within how many hours: ")) * 60
            except (ValueError, EOFError): continue
            _, A = _claude_list(flt, mins=mins)
            for _mt, sid, cwd, _t, _p in A: _claunch(sid, cwd)
            print(f"+ restored {len(A)}"); return
        if x.isdigit() and 1 <= int(x) <= len(L):
            _mt, sid, cwd, _t, _p = L[int(x) - 1]; _claunch(sid, cwd); return
        flt = x                                       # anything else = search filter


RQ = r'''LIVE=$(tmux list-panes -s -t a -F "#{pane_start_command}" 2>/dev/null|grep -oE "[0-9a-f]{8}-[0-9a-f-]{27}")
for j in $(ls -t ~/.claude/projects/*/*.jsonl 2>/dev/null|head -15);do s=$(basename "$j" .jsonl)
 c=$(grep -o "\"cwd\":\"[^\"]*\"" "$j" 2>/dev/null|head -1|cut -d\" -f4)
 p=$(grep -o "\"role\":\"user\",\"content\":\"[^\"]\{1,46\}" "$j" 2>/dev/null|grep -vE "local-command|command-name"|head -1|sed "s/.*content\":\"//")
 printf "%s|%s|%s|%s|%s\n" "$(echo "$LIVE"|grep -q "$s"&&echo 1||echo 0)" "$s" "${c:-?}" "$(stat -c %Y "$j" 2>/dev/null||stat -f %m "$j" 2>/dev/null||echo 0)" "${p:-?}";done'''


def _resume_attach(host, live, cwd, sid):             # parked → resume into the box's tmux (survives ssh drop), then attach
    if not live:
        subprocess.run(["a", "ssh", host, "tmux", "new-window", "-t", "a", "-c", cwd, "-n", "r-" + sid[:8],
                        "claude", "--dangerously-skip-permissions", "--model", "opus", "--resume", sid])
    os.execvp("a", ["a", "ssh", host])


def _scan(host):                                      # live agent work on one box (None=local): [(host,live,cwd,sid,mtime,desc)]
    cmd = ["bash", "-c", RQ] if host is None else ["a", "ssh", host, RQ]
    try: out = subprocess.run(cmd, capture_output=True, text=True, timeout=15).stdout
    except Exception: return []
    res = []
    for l in out.splitlines():
        if l.count("|") != 4: continue
        live, sid, cwd, mt, desc = l.split("|", 4)
        res.append((host or DEV, live == "1", cwd, sid, int(mt) if mt.isdigit() else 0, desc))
    return res


def _hosts():                                         # canonical remote box names from the ssh registry (dedup lan/wan/usb/hot variants)
    seen = set()
    for f in glob.glob(HOST % "*"):
        try: m = re.search(r"^Name:\s*(\S+)", open(f).read(), re.M)
        except OSError: m = None
        if not m: continue
        base = re.sub(r"-(lan|wan|usb|hot|relay)$", "", m.group(1))
        if base and base != DEV and not base.startswith(DEV): seen.add(base)
    return sorted(seen)


def remote(host):                                     # review one box's agent work LIVE over ssh; pick → resume(killed)/attach(live)
    rows = _scan(host)
    if not rows: print(f"(no agent transcripts on {host} — reachable?)"); return
    print(f"\n\033[1m{host}\033[0m — agent work  (● live=attach · ⏸ parked=resume):")
    for i, (_h, live, cwd, _s, _mt, prev) in enumerate(rows, 1):
        print(f"{i:2d}) {'● live' if live else '⏸ park'} {os.path.basename(cwd) or '/':<12} {prev}")
    try: x = input(f"# {host}: pick # (resume/attach), q: ").strip()
    except EOFError: return
    if not (x.isdigit() and 1 <= int(x) <= len(rows)): return
    _h, live, cwd, sid, _mt, _p = rows[int(x) - 1]
    _resume_attach(host, live, cwd, sid)


def main(a):
    via = a[0] if a and a[0] in ("res", "resume", "snap") else ""   # how we were invoked
    if via: a = a[1:]
    cmd = a[0] if a else ("save" if via == "snap" else "")          # bare `a snap` = save (back-compat); res/resume = pick
    if cmd in ("save", "s"): save()
    elif cmd == "show": show(a[1] if len(a) > 1 else "")
    elif cmd == "restore": restore(dry=("--dry" in a or "-n" in a))
    elif cmd in ("", "pick"): pick()
    elif os.path.exists(HOST % cmd): remote(cmd)
    else: print("usage: a res [ | save | show | restore [--dry] | <host> ]")


if __name__ == "__main__":
    main(sys.argv[1:])
