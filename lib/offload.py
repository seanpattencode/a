#!/usr/bin/env python3
"""a offload — note → LLM plan → you approve → agent runs it. Resumable active list.

  a offload <#|text>   propose a plan for the Nth pending note (a n l order) or literal text
  a offload            list active jobs (proposed/running) — pick one to resume/view
  a offload <id>       show one job; print how to resume its agent

A workflow script (plain py + claude + `a`), NOT a new `a` primitive — lib/offload.py
is already `a offload` via a.c dispatch, so it's deletable (the reversible default).
Two-phase safety: planning is auto + read-only (can't harm goals); the mutating run is
gated on your [y]. Non-interactive runs only ever save a 'proposed' job, never launch.
Jobs are kv files under adata/git/offload → synced, so a RAM-kill/crash is resumable.
"""
import sys, os, glob, time, subprocess, platform

DEV = platform.node() or "?"
ROOT = next((p for p in (os.path.expanduser("~/a/adata/git"), os.path.expanduser("~/adata/git"))
             if os.path.isdir(p + "/notes")), os.path.expanduser("~/a/adata/git"))
OFF, NOTES = ROOT + "/offload", ROOT + "/notes"

PLAN_PROMPT = """Plan the work in the note below for an autonomous coding agent. Output ONLY the plan, no preamble.
- Line 1: the goal in one sentence.
- Then numbered steps, smallest plan that achieves it (this is `a`'s minimize-to-triviality ethos).
- Tag each step [safe] (reversible/read-only: read, research, draft, local edits behind review) or
  [commit] (irreversible/outward: push, send, delete, deploy — anything that could affect goals/others).
- Last line "RISKS: ..." — what could harm the user's goals if done wrong.

NOTE:
"""


def gen_plan(note):
    env = {**os.environ}; env.pop("CLAUDECODE", None); env.pop("CLAUDE_CODE_ENTRYPOINT", None)
    try:
        r = subprocess.run(["claude", "-p", "--dangerously-skip-permissions"],
                           input=PLAN_PROMPT + note, capture_output=True, text=True, env=env, timeout=180)
    except subprocess.TimeoutExpired:
        return "x plan timed out"
    return r.stdout.strip() or ("x no plan\n" + r.stderr)


def note_by_n(n):                                   # Nth pending note, same order as `a n l`
    js = []
    for f in glob.glob(NOTES + "/*.txt"):
        kv = dict(l.split(": ", 1) for l in open(f, errors="replace").read().splitlines() if ": " in l)
        if kv.get("Status", "pending") == "pending" and kv.get("Text"):
            js.append((f.rsplit("_", 1)[-1], kv["Text"]))
    js.sort()
    return js[n - 1][1] if 1 <= n <= len(js) else None


def save(note, plan, status, **extra):
    os.makedirs(OFF, exist_ok=True)
    ts = time.strftime("%Y%m%dT%H%M%S")
    fn = f"{OFF}/{os.urandom(4).hex()}_{ts}.txt"
    hdr = {"Text": note.replace("\n", " "), "Status": status, "Created": ts, "Device": DEV, **extra}
    open(fn, "w").write("".join(f"{k}: {v}\n" for k, v in hdr.items()) + "---\n" + plan + "\n")
    return fn


def load(fn):
    hdr, _, plan = open(fn, errors="replace").read().partition("\n---\n")
    kv = dict(l.split(": ", 1) for l in hdr.splitlines() if ": " in l)
    return kv, plan.strip()


def setkv(fn, **kw):
    kv, plan = load(fn)
    kv.update(kw)
    open(fn, "w").write("".join(f"{k}: {v}\n" for k, v in kv.items()) + "---\n" + plan + "\n")


def launch(fn):                                     # gated: run the approved plan as a bg `a job`
    kv, plan = load(fn)
    folder = kv.get("Folder", os.getcwd())
    if os.environ.get("A_OFFLOAD_DRY"):
        print(f"[dry] cd {folder} && a job <plan {len(plan)}b>"); setkv(fn, Status="running", Session="dry"); return
    r = subprocess.run(["a", "job", plan], cwd=folder, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sn = next((l.split("· ", 1)[1].strip() for l in r.stdout.splitlines() if "· " in l), "?")
    setkv(fn, Status="running", Session=sn)
    print(f"✓ running · job {os.path.basename(fn).split('_')[0]} · resume: tmux select-window -t a:{sn}")


def jobs():
    return sorted(glob.glob(OFF + "/*.txt"), key=lambda f: f.rsplit("_", 1)[-1], reverse=True)


def show(fn):
    kv, plan = load(fn)
    print(f"━━━ {os.path.basename(fn).split('_')[0]} [{kv.get('Status')}]  {kv.get('Text','')[:70]}\n{plan}")
    if kv.get("Session") and kv["Session"] not in ("", "dry", "?"):
        print(f"\nresume: tmux select-window -t a:{kv['Session']}   (or: a resume)")


def propose(note):
    print(f"planning…  ({len(note)}b note)", file=sys.stderr)
    plan = gen_plan(note)
    fn = save(note, plan, "proposed", Folder=os.getcwd())
    print(f"━━━ plan ({os.path.basename(fn).split('_')[0]})\n{plan}\n")
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print("saved 'proposed' (non-interactive — not launched). approve later: a offload " +
              os.path.basename(fn).split('_')[0]); return
    while True:
        sys.stdout.write("\n[y] approve & run   [e] edit   [n] keep as proposed: "); sys.stdout.flush()
        k = (sys.stdin.readline() or "n").strip().lower()[:1]
        if k == "y": return launch(fn)
        if k == "e":
            subprocess.call([os.environ.get("EDITOR", "nano"), fn]); show(fn); continue
        print("kept proposed:", os.path.basename(fn).split('_')[0]); return


def main(a):
    if not a:                                       # list active jobs
        js = [f for f in jobs() if load(f)[0].get("Status") in ("proposed", "running")]
        if not js: print("no active jobs.  a offload <#|text>  to start one"); return
        for f in js:
            kv, _ = load(f)
            print(f"  {os.path.basename(f).split('_')[0]}  {kv.get('Status'):8}  {kv.get('Text','')[:64]}")
        return
    arg = a[0]
    hit = next((f for f in jobs() if os.path.basename(f).startswith(arg + "_")), None)  # job id?
    if hit: return show(hit)
    if arg.isdigit():                               # Nth pending note
        note = note_by_n(int(arg))
        if not note: print(f"x no pending note #{arg} (a n l)"); return
        return propose(note)
    return propose(" ".join(a))                     # literal text


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "offload": a = a[1:]            # `a offload …` forwards the tool name as argv[1]; drop it
    main(a)
