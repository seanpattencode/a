#!/usr/bin/env python3
"""a kil [query] — kill an APP in one glance. Rows are programs, not processes.

  a kil          every app, biggest-RAM first
  a kil chrome   jump straight to it
  a kil -9 …     SIGKILL instead of the default clean SIGTERM
One row per program = total RAM + process count + PIDs. You never read a
renderer/zygote/crashpad line again — pick the app, the whole tree dies at once.
Enter SIGTERMs every PID in the chosen app (clean shutdown, lets it save);
Esc / no selection = nothing happens.
"""
import os, sys, signal, subprocess
from collections import defaultdict

def appname(exe):                                     # collapse a process's exe path to its app identity
    tok = exe.split()[0] if exe.split() else ""        # the executable, ignoring its flags/paths
    base = tok.split("/")[-1] or "?"
    if "chrome-canary" in exe: return "chrome-canary"  # split Canary from stable (both have comm "chrome")
    if "/chrome/" in exe:      return "chrome-stable"
    return base

def apps():                                           # name -> (total_rss_kb, [pids]); biggest RAM first
    out = subprocess.run(["ps", "-eo", "rss,pid,args", "--sort=-rss"],
                         capture_output=True, text=True).stdout.splitlines()
    rss, pids = defaultdict(int), defaultdict(list)
    for line in out[1:]:
        f = line.split(None, 2)
        if len(f) < 3: continue
        n = appname(f[2]); rss[n] += int(f[0]); pids[n].append(f[1])
    return sorted(rss, key=lambda n: -rss[n]), rss, pids

def main():
    args = sys.argv[1:]
    if args and args[0] in ("kil", "kill"): args = args[1:]  # `a kil` forwards its own name as argv[1] — not a query
    sig = signal.SIGKILL if "-9" in args else signal.SIGTERM  # -9 = force; default lets apps save
    query = " ".join(a for a in args if a != "-9")
    order, rss, pids = apps()
    if not order: print("no processes"); return
    rows = [f"{rss[n]/1024:8.1f} MB  {len(pids[n]):3d} procs  {n}" for n in order]
    hdr = f"Enter = {'SIGKILL' if sig == signal.SIGKILL else 'SIGTERM'} the whole app  ·  Esc = cancel"
    cmd = ["fzf", "--reverse", "--header=" + hdr]
    if query: cmd += ["--query", query]
    sel = subprocess.run(cmd, input="\n".join(rows), capture_output=True, text=True)
    if sel.returncode != 0 or not sel.stdout.strip():
        print("cancelled"); return
    name = sel.stdout.split("procs", 1)[-1].strip()    # the app column
    targets = pids.get(name, [])
    killed = kill_app(targets, sig)
    print(f"killed {name}: {killed}/{len(targets)} procs ({rss[name]/1024:.0f} MB) — pids {' '.join(targets)}")

def kill_app(pids, sig=signal.SIGTERM):               # SIGNAL every pid in the tree; returns how many landed
    killed = 0
    for p in pids:
        try: os.kill(int(p), sig); killed += 1
        except (ProcessLookupError, PermissionError, ValueError): pass
    return killed

if __name__ == "__main__":
    main()
