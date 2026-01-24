"""aio scan - Scan for repos"""
import sys, os, subprocess as sp, json, shutil
from pathlib import Path
from . _common import init_db, load_proj, add_proj, auto_backup

def run():
    init_db()
    args = sys.argv[2:]; gh_mode = 'gh' in args or 'github' in args; args = [a for a in args if a not in ('gh', 'github')]
    sel = next((a for a in args if a.isdigit() or a == 'all' or '-' in a and a.replace('-','').isdigit()), None)
    if not gh_mode and not args and shutil.which('gh'):
        print("Scan: 1=local repos  2=GitHub"); ch = input("> ").strip()
        if ch == '2': gh_mode = True
        elif ch != '1': return
    if gh_mode:
        r = sp.run(['gh', 'repo', 'list', '-L', '50', '--json', 'name,url,pushedAt'], capture_output=True, text=True); repos = sorted(json.loads(r.stdout) if r.returncode == 0 else [], key=lambda x: x.get('pushedAt',''), reverse=True)
        cloned = {os.path.basename(p) for p in load_proj()}; repos = [(r['name'], r['url'], r.get('pushedAt','')[:10]) for r in repos if r['name'] not in cloned]
        if not repos: print("No new GitHub repos"); return
        for i, (n, u, d) in enumerate(repos): print(f"  {i}. {n:<25} {d}")
        if not sel: sel = input("\nClone+add (#, #-#, 'all', or q): ").strip() if sys.stdin.isatty() else 'q'
        if sel in ('q', ''): return
        idxs = list(range(len(repos))) if sel == 'all' else [j for x in sel.replace(',', ' ').split() for j in (range(int(x.split('-')[0]), int(x.split('-')[1])+1) if '-' in x else [int(x)]) if 0 <= j < len(repos)]
        pd = os.path.expanduser('~/projects'); os.makedirs(pd, exist_ok=True)
        for i in idxs: n, u, _ = repos[i]; dest = f"{pd}/{n}"; r = sp.run(['gh', 'repo', 'clone', u, dest], capture_output=True, text=True); ok, _ = add_proj(dest) if r.returncode == 0 or os.path.isdir(dest) else (False, ''); print(f"{'✓' if ok else 'x'} {n}")
    else:
        default = next((p for p in ['~/projects', '~/storage/shared', '~'] if os.path.isdir(os.path.expanduser(p))), '~')
        d = os.path.expanduser(next((a for a in args if a not in (sel,) and not a.startswith('-')), default)); existing = set(load_proj())
        repos = sorted([p.parent for p in Path(d).rglob('.git') if p.exists() and str(p.parent) not in existing and '/.cargo/' not in str(p) and '/lazy/' not in str(p) and '/aiosWorktrees/' not in str(p)], key=lambda x: x.name.lower())[:50]
        if not repos: print(f"No new repos in {d}"); return
        for i, r in enumerate(repos): print(f"  {i}. {r.name:<25} {str(r)}")
        if not sel: sel = input("\nAdd (#, #-#, 'all', or q): ").strip() if sys.stdin.isatty() else 'q'
        if sel in ('q', ''): return
        idxs = list(range(len(repos))) if sel == 'all' else [j for x in sel.replace(',', ' ').split() for j in (range(int(x.split('-')[0]), int(x.split('-')[1])+1) if '-' in x else [int(x)]) if 0 <= j < len(repos)]
        for i in idxs: ok, _ = add_proj(str(repos[i])); print(f"{'✓' if ok else 'x'} {repos[i].name}")
    auto_backup() if idxs else None
