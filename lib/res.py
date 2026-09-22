#!/usr/bin/env python3
"""a res — resume agents: bare = interactive pick; save/show/restore = tmux snapshot across a reboot (same cmd: a resume/a snap).
Window -> (name,cwd,cmd): claude/codex/agy/grok resume their session (claude launch id goes stale on compaction), `a ssh` host
windows reconnect, rest reopen as shells. Restore fires on session-create (tm_ensure_sess); A_SNAP_SESSION overrides.
GUI (sway): reopen AT MOST ONE foot+firefox, never the saved count (5 foots squashed ws1, 9/11); no sway -> tmux only."""
import sys, os, json, glob, re, socket, subprocess, time

DEV = socket.gethostname()
TMS = os.environ.get("A_SNAP_SESSION", "a")          # a's tmux session (overridable for testing)
GIT = os.path.expanduser("~/a/adata/git")
SNAPDIR = os.path.expanduser("~/a/adata/local/sessions")  # machine-rewritten: local, never git (Sean 09-11); .prev = undo
SNAP = f"{SNAPDIR}/{DEV}.json"
PROJ = os.path.expanduser("~/.claude/projects")
ID = re.compile(r"--(?:resume|session-id)[ =]+([0-9a-f-]{36})")   # session id on a claude cmdline
try: C = dict(re.findall(r"^m_(\w+): *(.*)", open(f"{GIT}/workspace/config.txt").read(), re.M))
except OSError: C = {}
MF = "".join(f" --{k} {C[k]}" for k in ("model", "effort") if C.get(k)) if C.get("agent", "claude") == "claude" else ""
RESUME = {"claude": f"claude --dangerously-skip-permissions{MF} --resume %s; exec bash",  # %s=sid; no MF → settings.json default
          "codex": "codex resume --last; exec bash", "agy": "agy --dangerously-skip-permissions -c; exec bash",
          "grok": "grok --always-approve --continue; exec bash"}   # --continue = cwd's newest session
HOST = f"{GIT}/ssh/%s.txt"                            # a ssh host registry


LINUX = os.path.isdir("/proc")
PS = {}                                               # macOS/BSD: {pid: (ppid, command)} from one `ps`


def _ps():                                            # populate PS on platforms without /proc (macOS)
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


AEXE = {"claude", "codex", "agy", "grok"}          # agent binaries we revive natively

def agent(pids):                                      # (kind, sid) of the agent under these panes; (None, "") if none
    for p in (t for pid in pids for t in tree(pid)):  # match the LAUNCHED BINARY, never a prompt substring:
        cl = _cmdline(p)                              # every agent carries the tool-list in argv, so "x in cl" false-matches
        for tk in cl.split("\0")[:2]:                 # argv[0:2] = exe (+ `node <script>`); the prompt sits later
            b = os.path.basename(tk)
            if b in AEXE:
                if b == "claude": m = ID.search(cl); return ("claude", m.group(1) if m else "")
                return (b, "")
    return (None, "")


def have(sid): return bool(sid and glob.glob(f"{PROJ}/*/{sid}.jsonl"))


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
        w.setdefault(i, [i, n, cwd, [], sc])[3].append(pid)
    return list(w.values())


def save():
    if not LINUX: _ps()                               # macOS: snapshot the process table once
    cur = subprocess.run(["tmux", "display-message", "-p", "#{window_id}"], capture_output=True,
                         text=True).stdout.strip() if os.environ.get("TMUX") else ""
    info = [(wid, n, cwd, *agent(pid), sc) for wid, n, cwd, pid, sc in windows()]  # (id, name, cwd, kind, sid, sc)
    if not info: print("x no windows — snapshot kept"); return []  # don't clobber with emptiness
    claimed = {sid for _w, _n, _c, k, sid, _s in info if k == "claude" and have(sid)}  # claude ids with a transcript
    used, jobs, here = set(), [], []
    for wid, name, cwd, kind, sid, sc in info:
        if "while a i" in sc: continue                             # skip a's session-keeper window
        cmd = s = ""                                               # unknown window → shell
        if kind == "claude":                                       # claude → resume its real transcript
            s = sid if have(sid) else newest_in(cwd, claimed | used)
            if s and s not in used: used.add(s); cmd = RESUME["claude"] % s
        elif kind in ("codex", "agy", "grok"): cmd = RESUME[kind]   # native resume
        elif os.path.exists(HOST % name): cmd = "a ssh %s; exec bash" % name   # ssh window → reconnect
        jobs.append({"window": name, "cwd": cwd, "cmd": cmd,
                     "preview": (_preview(s) if s else "") or _pane_tail(wid)})  # bake tail → syncs cross-device; pane tail when transcript is silent
        here.append("→" if wid == cur else " ")                    # the window you're running this from
    os.makedirs(SNAPDIR, exist_ok=True)
    gui = gui_save()
    if not gui:                                       # sway down mid-save — keep last known gui (don't clobber with emptiness)
        try: gui = json.load(open(SNAP)).get("gui", [])
        except (OSError, ValueError): gui = []
    if os.path.exists(SNAP): os.replace(SNAP, SNAP + ".prev")   # one-step undo
    json.dump({"host": DEV, "session": TMS, "jobs": jobs, "gui": gui}, open(SNAP, "w"), indent=1)
    print(f"✓ snapshot {len(jobs)} window(s) + {len(gui)} gui · {time.strftime('%Y-%m-%d %H:%M')} → {SNAP}")
    for m, j in zip(here, jobs):
        tag = j["cmd"].split()[0] if j["cmd"] else "(shell)"       # what respawns: claude/codex/agy/a/(shell)
        print(f" {m} {j['window']:16.16} {tag:8.8} {j['preview'][:56]}")
    return jobs


def _sway(*args):                                     # swaymsg passthrough (socket auto-discovered); None if no sway
    s = os.environ.get("SWAYSOCK") or (sorted(glob.glob(f"/run/user/{os.getuid()}/sway-ipc.*.sock"),
                                              key=os.path.getmtime, reverse=True) or [None])[0]
    if not s: return None
    m = next((x for x in ("/opt/sway-dev/bin/swaymsg", "/usr/local/bin/swaymsg") if os.path.exists(x)), "swaymsg")
    try: return subprocess.run([m, *args], capture_output=True, text=True,
                               env={**os.environ, "SWAYSOCK": s}).stdout
    except OSError: return None


GAPPS = {"foot", "firefox"}                           # gui apps we snapshot/reopen (sway app_ids)

def _gwalk(n, ws, acc):                               # (app family, ws) per real gui window; firefox-nightly counts as firefox
    if n.get("type") == "workspace": ws = n.get("name", ws)
    app = (n.get("app_id") or "").split("-")[0]
    if app in GAPPS and n.get("pid"): acc.append({"app": app, "ws": ws})
    for c in n.get("nodes", []) + n.get("floating_nodes", []): _gwalk(c, ws, acc)


def gui_save():                                       # sway gui windows on record; restore caps at one of each
    t = _sway("-t", "get_tree")
    if not t: return []
    acc = []
    _gwalk(json.loads(t), "", acc)
    return acc


def gui_restore(gui, dry=False):                      # AT MOST ONE foot + one firefox, never the saved count (5 foots squashed ws1, 09-11)
    if not gui: return
    t = _sway("-t", "get_tree")
    if t is None: print("(no sway — gui skipped)"); return
    have = []; _gwalk(json.loads(t), "", have); have = {h["app"] for h in have}
    for app, run in (("foot", "foot tmux attach -t " + TMS), ("firefox", "firefox")):
        g = next((x for x in gui if x["app"] == app), None)
        if not g: continue
        if app in have: print(f"  gui: {app} already open"); continue
        print(f'  {"[dry] " if dry else ""}↻ {app} → ws {g["ws"]} (one only)')
        if not dry: _sway(f'workspace {g["ws"]}; exec {run}')


def _pane_tail(wid):                                  # last non-blank visible line of a window → identifies shells
    r = subprocess.run(["tmux", "capture-pane", "-p", "-t", wid], capture_output=True, text=True)
    for l in reversed(r.stdout.splitlines()):
        if any(c.isalnum() for c in l) and "⏵" not in l and "shift+tab" not in l:  # skip claude chrome/separators
            return re.sub(r"\s+", " ", l.strip())[:60]
    return ""


def _preview(sid):                                   # tail (last 64KB) of a claude transcript → last human line = "what it's about"
    for fp in glob.glob(f"{PROJ}/*/{sid}*.jsonl"):
        try:
            with open(fp, "rb") as fh:
                fh.seek(0, 2); fh.seek(max(0, fh.tell() - 65536)); txt = fh.read().decode("utf-8", "ignore")
        except OSError: continue
        if m := _hum(txt): return re.sub(r"\s+", " ", m[-1])[:60]
    return ""


def _live():                                         # names of currently-open local tmux windows (● = live)
    r = subprocess.run(["tmux", "list-windows", "-t", TMS, "-F", "#{window_name}"], capture_output=True, text=True)
    return set(r.stdout.split())


def show(flt=""):                                     # per-device saved windows (local; live remote: a res <host>)
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
            desc = (_preview(sid[1]) if here and sid else "") or j.get("preview", "") \
                or (("claude " + sid[1][:7]) if sid else (j["cmd"][:44] or "(shell)"))   # what it's about
            print(f"  {mark} {j['window']:15.15} {os.path.basename(j['cwd']):<8} {desc[:58]}"); n += 1
    print(f"\n{n} window(s) · {len(files)} device(s){'  /'+flt if flt else '   search: a res show <text>'}")


def restore(dry=False):
    if not os.path.exists(SNAP):
        print("(no snapshot to restore)"); return
    d = json.load(open(SNAP)); jobs = d["jobs"]
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
    gui_restore(d.get("gui", []), dry)
    print(f"{'[dry] ' if dry else ''}✓ restored {n} window(s) into tmux '{TMS}'")


def _age(s):
    s = int(s); return f"{s//60}m" if s < 3600 else (f"{s//3600}h" if s < 86400 else f"{s//86400}d")


def _snip(ut, q, w):                                  # the line that MATCHED, match underlined; no q → last message
    i = ut.lower().find(q or "\0")
    s = re.sub(r"\s+", " ", ut[max(0, i - 22):i + w] if i >= 0 else ut.rsplit("\n", 1)[-1]).strip()[:max(10, w)]
    j = s.lower().find(q or "\0")
    return f"{s[:j]}\x1b[4m{s[j:j+len(q)]}\x1b[24m{s[j+len(q):]}" if j >= 0 else s


def _stamp(t): l = time.localtime(t); return f"{time.strftime('%b', l)}{l.tm_mday}-{l.tm_hour % 12 or 12}{l.tm_min:02d}{'ap'[l.tm_hour > 11]}"   # Sep4-346p = tm_name (tmux.c): the WHEN in every agent window name
def _claunch(r):                                      # resume one claude transcript row [mt, sid, cwd, ..] in a window named by ITS date
    subprocess.run(["tmux", "new-window", "-n", f"r-{os.path.basename(r[2])}-{_stamp(r[0])}", "-c", r[2], RESUME["claude"] % r[1]])


def _hum(t): return [x for x in re.findall(r'"role":"user","content":"([^"]{2,400})', t) if not x.startswith(("<", "[Request"))]


def _load(cut=0):                                     # rows [mt, sid, cwd, turns, text]; typed sessions only (claude -p = sdk-cli); cut=mtime floor else newest 150
    rows = []
    for f in sorted(glob.glob(f"{PROJ}/*/*.jsonl"), key=os.path.getmtime, reverse=True)[:None if cut else 150]:
        if os.path.getmtime(f) < cut: break
        try: txt = open(f, errors="ignore").read()
        except OSError: continue
        ms, c = _hum(txt), re.search(r'"entrypoint":"([^"]+)","cwd":"([^"]+)"', txt)
        if ms and c and c[1] == "cli" and os.path.isdir(c[2]):
            rows.append([os.path.getmtime(f), os.path.basename(f)[:-6], c[2], len(ms),
                         "\n".join(ms).replace('\\"', '"').replace("\\n", " ")])
    return rows




def pick():                                           # resume picker: menu keys by default; [/]=search, [h]=revive ≤hrs
    if not os.environ.get("TMUX"): print("x need tmux"); return
    import termios, tty, select
    t1 = time.perf_counter_ns(); rows = _load()       # his words, not tool blobs
    if not sys.stdin.isatty():                        # scripted use: print once, never hang
        for r in rows[:20]: print(f"{_age(time.time()-r[0]):>4} {r[3]:3d}t {os.path.basename(r[2]):<12} {_snip(r[4], '', 58)}")
        return
    corp = [(r, (os.path.basename(r[2]) + "\n" + r[4]).lower()) for r in rows]
    NAT = {"c": ("codex", "codex resume; exec bash"), "g": ("agy", RESUME["agy"]),
           "k": ("grok", "grok --always-approve --resume; exec bash")}
    q, sel, act, mode = "", 0, None, ""
    old = termios.tcgetattr(0)
    sys.stdout.write("\x1b[?1049h\x1b[?25l")
    try:
        tty.setcbreak(0)
        while True:
            ql = q.lower() if mode == "/" else ""     # rank: most user-text hits first, then newest; best sits AT the menu
            m = sorted(((r, l) for r, l in corp if ql in l), key=lambda x: (-x[1].count(ql), -x[0][0])) if ql else corp
            W, H = os.get_terminal_size()
            items = [r for r, _l in reversed(m[:H - 5])]
            sel = max(0, min(sel, len(items) - 1))
            o = "\x1b[H" + "\x1b[K\n" * (H - 1 - len(items))
            for i, it in enumerate(items):
                ln = f" {_age(time.time()-it[0]):>4} {it[3]:>4}t {os.path.basename(it[2]):<10.10} {_snip(it[4], ql, W-24)}"
                o += ("\x1b[7m" if i == len(items) - 1 - sel else "") + ln + "\x1b[0m\x1b[K\n"
            hh = float(v) if (v := q.rstrip("h")).replace(".", "", 1).isdigit() else 0
            st = f"hrs: {q}▌ [ revive ALL {sum(r[0] >= time.time() - hh * 3600 for r in rows)} ≤{hh:g}h ] ↵=go esc=back" if mode == "h" \
                else f"/{q}▌ {len(m)}/{len(rows)} ↵=open esc=back" if mode == "/" \
                else "[/]search  [h]revive ≤hrs  [c]odex  [g]=agy  [k]grok  ↑↓↵ open  [q]uit"
            o += (st + f" · {(time.perf_counter_ns()-t1)/1e6:.4f}ms")[:W] + "\x1b[K"
            sys.stdout.write(o); sys.stdout.flush()
            b = os.read(0, 1); t1 = time.perf_counter_ns()
            if b in b"\x03\x04": return               # b""=EOF exits
            if b == b"\x1b":
                if not select.select([0], [], [], 0.02)[0]:
                    if mode: mode = q = ""; continue
                    return
                b2 = os.read(0, 2); sel += b2.endswith(b"A") - b2.endswith(b"B")
            elif b in b"\r\n":
                if mode == "h":
                    if hh: act = ("hrs", hh); break
                elif items: act = items[len(items) - 1 - sel]; break
            elif b in b"\x7f\x08": q, sel = q[:-1], 0
            elif not mode:
                if b == b"q": return
                if (a := NAT.get(b.decode("utf-8", "ignore"))): act = a; break
                if b in b"/h": mode, q = b.decode(), ""
                elif b.isdigit(): mode, q = "h", b.decode()
            elif b >= b" ": q, sel = q + b.decode("utf-8", "ignore"), 0
    finally:
        termios.tcsetattr(0, termios.TCSADRAIN, old); sys.stdout.write("\x1b[?1049l\x1b[?25h"); sys.stdout.flush()
    if isinstance(act, list): _claunch(act)
    elif act and act[0] == "hrs":
        rs = _load(time.time() - act[1] * 3600)
        for r in rs: _claunch(r)
        print(f"+ revived {len(rs)} ≤{act[1]:g}h")
    elif act: subprocess.run(["tmux", "new-window", "-n", f"r-{act[0]}-{_stamp(time.time())}", act[1]])


RQ = r'''LIVE=$(tmux list-panes -s -t a -F "#{pane_start_command}" 2>/dev/null|grep -oE "[0-9a-f]{8}-[0-9a-f-]{27}")
for j in $(ls -t ~/.claude/projects/*/*.jsonl 2>/dev/null|head -15);do s=$(basename "$j" .jsonl)
 c=$(grep -o "\"cwd\":\"[^\"]*\"" "$j" 2>/dev/null|head -1|cut -d\" -f4)
 p=$(grep -o "\"role\":\"user\",\"content\":\"[^\"]\{1,46\}" "$j" 2>/dev/null|grep -vE "local-command|command-name"|head -1|sed "s/.*content\":\"//")
 printf "%s|%s|%s|%s|%s\n" "$(echo "$LIVE"|grep -q "$s"&&echo 1||echo 0)" "$s" "${c:-?}" "$(stat -c %Y "$j" 2>/dev/null||stat -f %m "$j" 2>/dev/null||echo 0)" "${p:-?}";done'''


def _resume_attach(host, live, cwd, sid, mt):         # parked → resume into the box's tmux (survives ssh drop), then attach
    if not live:
        subprocess.run(["a", "ssh", host, "tmux", "new-window", "-t", "a", "-c", cwd, "-n", f"r-{os.path.basename(cwd)}-{_stamp(mt)}",
                        f"claude --dangerously-skip-permissions{MF} --resume {sid}"])
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
    _resume_attach(host, live, cwd, sid, _mt)


def _row(n):
    try: f = open(os.path.expanduser('~/a/adata/local/done.log')).read().splitlines()[int(n)].split('\t', 4); return f[2], f[3], int(f[0]), f[4][-50:]
    except Exception: return '', '', 0, ''

def _first(t, k):  # first prompt of the transcript with k before t
    try:
        return next(json.loads(re.search(r'"user","content":("(?!<)([^"\\]|\\.)*")', s)[1]) for f in glob.glob(PROJ + '/*/*.jsonl')
                    if os.path.getmtime(f) >= t and k in (s := open(f).read()) and any(k in l and json.loads(l)['timestamp'] < time.strftime('%FT%T', time.gmtime(t + 9)) for l in s.splitlines()))
    except Exception: return ''

def watch(n):                                         # /review SSE: push on events only (pidfd exit, transcript/snapshot inotify)
    import select, ctypes, signal; signal.signal(signal.SIGPIPE, signal.SIG_DFL); C = ctypes.CDLL(None); last = ''
    name, cwd, t, m = _row(n); p = None
    while True:
        s = 'data: %s\n' % json.dumps(dict(review(n), prompt=p or ''))
        if s != last: last = s; print(s, flush=True)
        if p is None: p = _first(t, m); continue
        if not LINUX: time.sleep(60); continue
        ap = next((int(p) for w in windows() if w[1:3] == [name, cwd] for q in w[3] for p in tree(q) if any(os.path.basename(t) in AEXE for t in _cmdline(p).split('\0')[:2])), 0)
        pfd = max(0, C.syscall(434, ap, 0)); ifd = C.inotify_init1(0)   # micromamba py lacks os.pidfd_open
        for d in (PROJ + '/' + ''.join(c if c.isalnum() else '-' for c in cwd), SNAPDIR): C.inotify_add_watch(ifd, d.encode(), 0x3c2)
        if select.select([ifd] + [pfd] * (pfd > 0), [], [], 240)[0]: time.sleep(0.6)
        else: print(':\n', flush=True)                # dead-client reap
        os.close(ifd); pfd and os.close(pfd)

def review(n, revive=False):
    name, cwd, *_ = _row(n)
    if not LINUX: _ps()
    ws = [w for w in windows() if w[1:3] == [name, cwd]]
    panes = subprocess.run(['tmux', 'list-panes', '-t', ws[0][0], '-F', '#{pane_id}\t#{pane_pid}\t#{window_index}'], capture_output=True, text=True).stdout.splitlines() if len(ws) == 1 else []
    live = [p.split('\t') for p in panes if agent([p.split('\t')[1]])[0]]
    try: saved = [j for j in json.load(open(SNAP))['jobs'] if j['window'] == name and j['cwd'] == cwd and j['cmd']]
    except (OSError, ValueError): saved = []
    state, preview, win = 'UNAVAILABLE', 'No saved agent for this review.', ''
    if len(live) == 1:
        p, _, win = live[0]; state = 'ALIVE'
        preview = subprocess.run(['tmux', 'capture-pane', '-pJ', '-t', p, '-S', '-30'], capture_output=True, text=True).stdout.rstrip(); preview = '\n'.join([l.rstrip() for l in preview.splitlines() if any(c.isalnum() for c in l) and 'shift+tab' not in l and not re.fullmatch(r'\s*\d+ tokens', l)][-8:])
    elif not live and len(ws) < 2 and len(saved) == 1:
        state, preview = 'RESUMABLE', 'Saved output: ' + saved[0].get('preview', '')
        if revive: subprocess.run(['tmux', 'new-window', '-d', '-n', name, '-c', cwd, 'sh', '-c', saved[0]['cmd']], check=True); state = 'RESUMING'
    return dict(state=state, preview=preview, window=win)

def main(a):
    via = a[0] if a and a[0] in ("res", "resume", "snap") else ""   # how we were invoked
    if via: a = a[1:]
    cmd = a[0] if a else ("save" if via == "snap" else "")          # bare `a snap` = save (back-compat); res/resume = pick
    if cmd in ("save", "s"): save()
    elif cmd == "show": show(a[1] if len(a) > 1 else "")
    elif cmd == "review": print(json.dumps(review(a[1], a[2:] == ['resume'])))
    elif cmd == "watch": watch(a[1])
    elif cmd == "restore": restore(dry=("--dry" in a or "-n" in a))
    elif cmd in ("", "pick"): pick()
    elif os.path.exists(HOST % cmd): remote(cmd)
    else: print("usage: a res [ | save | show | restore [--dry] | <host> ]")


if __name__ == "__main__":
    main(sys.argv[1:])
