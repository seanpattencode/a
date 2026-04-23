"""aio attach [#] - Reconnect to session"""
import sys, os
from _common import init_db, load_cfg, jl_read, tm

def run():
    init_db(); cfg = load_cfg(); sel = sys.argv[2] if len(sys.argv) > 2 else None
    FK_DIR = os.path.expanduser("~/a/adata/forks"); cwd = os.getcwd()
    if FK_DIR in cwd and (p := cwd.replace(FK_DIR + '/', '').split('/')) and len(p) >= 2 and tm.has(s := f"{p[0]}-{p[1]}"): tm.go(s)
    runs = [(r['id'], r['repo']) for r in jl_read('multi_runs')[-10:][::-1]]
    if not runs: print("No sessions"); return
    if sel and not sel.isdigit() and tm.has(sel): tm.go(sel); return
    if sel and sel.isdigit() and (i := int(sel)) < len(runs): tm.go(f"{os.path.basename(runs[i][1])}-{runs[i][0]}")
    for i, (rid, repo) in enumerate(runs): print(f"  {i}  {'●' if tm.has(f'{os.path.basename(repo)}-{rid}') else '○'} {os.path.basename(repo)}-{rid}")
    print("\nSelect:\n  aio attach 0")

run()
