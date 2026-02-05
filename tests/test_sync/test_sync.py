import subprocess as sp, time, shlex, random
from pathlib import Path

ROOT, DEVICES = Path(__file__).parent / 'devices', ('device_a', 'device_b', 'device_c')
MAX_RETRIES = 3

def q(p): return shlex.quote(str(p))

def setup(): sp.run(f'rm -rf {q(ROOT)} && mkdir -p {q(ROOT/"origin")} && git -C {q(ROOT/"origin")} init -q -b main --bare', shell=True); [sp.run(f'mkdir -p {q(ROOT/d)} && git -C {q(ROOT/d)} init -q -b main && git -C {q(ROOT/d)} remote add origin {q(ROOT/"origin")}', shell=True) for d in DEVICES]

def is_conflict(text):
    """Detect all conflict types"""
    t = text.lower()
    return any(x in t for x in ['conflict', 'diverged', 'rejected', 'overwritten', 'unmerged', 'aborting'])

def resolve_conflicts(d):
    """Auto-resolve conflicts: edit wins, restore deleted files, accept all changes"""
    # Check for unmerged files
    r = sp.run(f'cd {q(d)} && git diff --name-only --diff-filter=U', shell=True, capture_output=True, text=True)
    unmerged = r.stdout.strip().split('\n') if r.stdout.strip() else []
    for f in unmerged:
        if not f: continue
        # Edit wins: checkout theirs (remote version with edits), or ours if we edited
        sp.run(f'cd {q(d)} && git checkout --theirs {shlex.quote(f)} 2>/dev/null || git checkout --ours {shlex.quote(f)} 2>/dev/null || git rm {shlex.quote(f)}', shell=True, capture_output=True)
    # Accept all incoming for deleted files (edit wins = restore)
    sp.run(f'cd {q(d)} && git checkout --theirs . 2>/dev/null', shell=True, capture_output=True)
    sp.run(f'cd {q(d)} && git add -A', shell=True, capture_output=True)

def _sync(d, silent=False):
    """Sync with auto-resolution. Returns (success, had_conflict)"""
    had_conflict = False
    for attempt in range(MAX_RETRIES):
        # Always commit local changes first
        sp.run(f'cd {q(d)} && git add -A && git commit -qm sync', shell=True, capture_output=True)
        # Pull with merge
        pull = sp.run(f'cd {q(d)} && git pull --no-rebase origin main', shell=True, capture_output=True, text=True)
        if is_conflict(pull.stderr + pull.stdout):
            had_conflict = True
            resolve_conflicts(d)
            sp.run(f'cd {q(d)} && git commit -qm "auto-resolve: edit wins"', shell=True, capture_output=True)
        # Push with retry
        push = sp.run(f'cd {q(d)} && git push -q origin main', shell=True, capture_output=True, text=True)
        if push.returncode == 0:
            return True, had_conflict
        if 'rejected' in (push.stderr + push.stdout).lower():
            had_conflict = True
            continue  # retry after pull
        return False, had_conflict
    return False, had_conflict

def create_file(device, name, content=''): ts=time.strftime('%Y%m%dT%H%M%S')+f'.{time.time_ns()%1000000000:09d}'; p=ROOT/device/f'{name}_{ts}.txt'; p.write_text(content or f'created by {device}'); sp.run(f'cd {q(ROOT/device)} && git add -A && git commit -qm "add {p.name}" && git push -u origin main', shell=True); return p.name

def pull(device):
    """Pull with auto-commit and conflict resolution"""
    d = ROOT/device
    sp.run(f'cd {q(d)} && git add -A && git commit -qm "pre-pull"', shell=True, capture_output=True)
    r = sp.run(f'cd {q(d)} && git pull --no-rebase origin main', shell=True, capture_output=True, text=True)
    if is_conflict(r.stderr + r.stdout):
        resolve_conflicts(d)
        sp.run(f'cd {q(d)} && git commit -qm "auto-resolve"', shell=True, capture_output=True)

def run_n(n): [(pull(d), create_file(d, f'note{i}'), [pull(x) for x in DEVICES]) for i in range(n) for d in DEVICES]; return {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}

def sync(device, silent=False):
    ts=time.strftime('%Y%m%dT%H%M%S')+f'.{time.time_ns()%1000000000:09d}'
    [p.rename(p.with_name(f'{p.stem}_{ts}{p.suffix}')) for p in (ROOT/device).glob('*.txt') if '_20' not in p.stem]
    pull(device)
    return _sync(ROOT/device, silent=silent)

def test_raw(n): [((ROOT/d/'samename.txt').write_text(f'v{i} {d}'), sync(d), [pull(x) for x in DEVICES]) for i in range(n) for d in DEVICES]; return {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}

def sync_edit(device, silent=False):
    ts=time.strftime('%Y%m%dT%H%M%S')+f'.{time.time_ns()%1000000000:09d}'
    arc=(ROOT/device/'.archive'); arc.mkdir(exist_ok=True)
    [((arc/p.name).write_text(sp.run(f'git -C {q(ROOT/device)} show HEAD:{p.name}',shell=True,capture_output=True,text=True).stdout), p.rename(p.with_name(f'{p.stem.rsplit("_",1)[0]}_{ts}{p.suffix}'))) for p in (ROOT/device).glob('*.txt') if sp.run(f'git -C {q(ROOT/device)} diff --quiet {p.name}',shell=True).returncode]
    pull(device)
    return _sync(ROOT/device, silent=silent)

def test_edit(n): setup(); f=create_file('device_a','doc'); [pull(d) for d in DEVICES]; [((ROOT/d/f).write_text(f'edit{i} {d}'), sync_edit(d), [pull(x) for x in DEVICES]) for i in range(n) for d in DEVICES]; return {'files':{d:len(list((ROOT/d).glob('*.txt'))) for d in DEVICES},'archive':{d:len(list((ROOT/d/'.archive').glob('*.txt'))) for d in DEVICES}}

def soft_delete(device, name):
    """Soft delete = archive (no hard deletes allowed)"""
    arc = ROOT/device/'.archive'; arc.mkdir(exist_ok=True)
    for p in (ROOT/device).glob(f'{name}*.txt'): p.rename(arc/p.name)
    return _sync(ROOT/device, silent=True)

def delete(device, name): return soft_delete(device, name)  # alias for compat

def test_delete(): setup(); f=create_file('device_a','todelete'); [pull(d) for d in DEVICES]; before={d:len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}; delete('device_a','todelete'); [pull(d) for d in DEVICES]; after={d:len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}; return {'before':before,'after':after}

def archive(device, name): arc=(ROOT/device/'.archive'); arc.mkdir(exist_ok=True); [p.rename(arc/p.name) for p in (ROOT/device).glob(f'{name}*.txt')]; pull(device); sp.run(f'cd {q(ROOT/device)} && git add -A && git commit -qm "archive {name}" && git push origin main', shell=True)

def test_archive(): setup(); f=create_file('device_a','toarchive'); [pull(d) for d in DEVICES]; archive('device_a','toarchive'); [pull(d) for d in DEVICES]; return {'files':{d:len(list((ROOT/d).glob('*.txt'))) for d in DEVICES},'archived':{d:len(list((ROOT/d/'.archive').glob('*.txt'))) for d in DEVICES}}

def test_offline(): setup(); f1,f2,f3,f4=[create_file('device_a',n) for n in ('toadd','todelete','toarchive','toedit')]; [pull(d) for d in DEVICES]; [(create_file('device_b',f'online{i}'), pull('device_b')) for i in range(2)]; pull('device_c'); (ROOT/'device_c'/'newfile.txt').write_text('add'); (ROOT/'device_c'/f2).unlink(); (ROOT/'device_c'/'.archive').mkdir(exist_ok=True); (ROOT/'device_c'/f3).rename(ROOT/'device_c'/'.archive'/f3); (ROOT/'device_c'/f4).write_text('edited'); sync_edit('device_c'); [pull(d) for d in DEVICES]; return {'files':{d:len(list((ROOT/d).glob('*.txt'))) for d in DEVICES},'archive':{d:len(list((ROOT/d/'.archive').glob('*.txt'))) for d in DEVICES}}

def monte_carlo(n=1000, verbose=False):
    setup(); create_file('device_a','seed'); [pull(d) for d in DEVICES]
    # Create nested folder on all devices
    for d in DEVICES: (ROOT/d/'nested').mkdir(exist_ok=True); sp.run(f'cd {q(ROOT/d)} && git add -A && git commit -qm "mkdir" && git push origin main', shell=True, capture_output=True)
    [pull(d) for d in DEVICES]
    online={d:True for d in DEVICES}; errors=[]; conflicts=[]; reseeds=0
    ops=['add','delete','archive','edit','toggle','same_name','edit_raw','nested','non_txt','direct_push']

    for i in range(n):
        d=random.choice(DEVICES); op=random.choice(ops)
        if op=='toggle': online[d]=not online[d]; continue
        if not online[d]: continue

        try:
            pull(d); files=list((ROOT/d).glob('*.txt'))

            # Handle edge case: all files deleted, add one back
            if not files:
                if verbose: print(f"  [{i}] All files deleted on {d}, reseeding...")
                (ROOT/d/f'reseed_{i}.txt').write_text(f'reseed {i}')
                sync(d, silent=True)
                reseeds += 1
                continue

            if op=='add':
                (ROOT/d/f'f{i}.txt').write_text(f'{i}')
                ok, conflict = sync(d, silent=True)
                if conflict: conflicts.append((i, d, 'add'))
            elif op=='delete' and files:
                arc = ROOT/d/'.archive'; arc.mkdir(exist_ok=True)
                f = random.choice(files); f.rename(arc/f.name)  # soft delete
                ok, conflict = _sync(ROOT/d, silent=True)
                if conflict: conflicts.append((i, d, 'delete'))
            elif op=='archive' and files:
                arc = ROOT/d/'.archive'; arc.mkdir(exist_ok=True)
                f = random.choice(files); f.rename(arc/f.name)
                ok, conflict = _sync(ROOT/d, silent=True)
                if conflict: conflicts.append((i, d, 'archive'))
            elif op=='edit' and files:
                random.choice(files).write_text(f'edit{i}')
                ok, conflict = sync_edit(d, silent=True)
                if conflict: conflicts.append((i, d, 'edit'))
            # NEW: same filename no timestamp on 2 devices
            elif op=='same_name':
                (ROOT/d/'collision.txt').write_text(f'{d}_{i}')
                ok, conflict = sync(d, silent=True)
                if conflict: conflicts.append((i, d, 'same_name'))
            # edit without sync_edit - uses _sync with auto-resolve
            elif op=='edit_raw' and files:
                random.choice(files).write_text(f'raw{i}')
                ok, conflict = _sync(ROOT/d, silent=True)
                if conflict: conflicts.append((i, d, 'edit_raw'))
            # NEW: nested folder files
            elif op=='nested':
                (ROOT/d/'nested'/f'n{i}.txt').write_text(f'{i}')
                ok, conflict = sync(d, silent=True)
                if conflict: conflicts.append((i, d, 'nested'))
            # non-txt files
            elif op=='non_txt':
                ext = random.choice(['.json','.md','.yaml'])
                (ROOT/d/f'file{i}{ext}').write_text(f'{i}')
                ok, conflict = _sync(ROOT/d, silent=True)
                if conflict: conflicts.append((i, d, 'non_txt'))
            # direct sync (all ops now use _sync with auto-resolve)
            elif op=='direct_push':
                (ROOT/d/f'direct{i}.txt').write_text(f'{i}')
                ok, conflict = _sync(ROOT/d, silent=True)
                if conflict: conflicts.append((i, d, 'direct_push'))

        except Exception as e: errors.append((i,d,op,str(e)))

    [pull(d) for d in DEVICES]
    counts={d:(len(list((ROOT/d).glob('*.txt'))),len(list((ROOT/d/'.archive').glob('*.txt')))if(ROOT/d/'.archive').exists()else 0,len(list((ROOT/d/'nested').glob('*.txt')))if(ROOT/d/'nested').exists()else 0,len(list((ROOT/d).glob('*.json'))+list((ROOT/d).glob('*.md'))+list((ROOT/d).glob('*.yaml')))) for d in DEVICES}
    by_op={op:len([c for c in conflicts if c[2]==op]) for op in ops}

    return {
        'actions': n,
        'errors': len(errors),
        'conflicts': len(conflicts),
        'by_op': {k:v for k,v in by_op.items() if v},
        'conflict_details': conflicts[:10] if conflicts else [],
        'reseeds': reseeds,
        'counts': counts,
        'match': len(set(c[:2] for c in counts.values()))==1  # compare txt+archive only
    }

def test_old_no_ts(): setup(); (ROOT/'device_a'/'note.txt').write_text('old no ts'); create_file('device_b','note'); [pull(d) for d in DEVICES if d!='device_a']; sync('device_a'); [pull(d) for d in DEVICES]; return {d:[p.name for p in (ROOT/d).glob('*.txt')] for d in DEVICES}

# === NEW CONFLICT TESTS ===

def test_race(n=5):
    """Two devices push without pull - auto-resolved via retry"""
    setup(); create_file('device_a','seed'); [pull(d) for d in DEVICES]
    resolved = []
    for i in range(n):
        # Both devices create files WITHOUT pulling
        (ROOT/'device_a'/f'race_a_{i}.txt').write_text(f'a{i}')
        (ROOT/'device_b'/f'race_b_{i}.txt').write_text(f'b{i}')
        # Both sync - should auto-resolve
        ok1, c1 = _sync(ROOT/'device_a', silent=True)
        ok2, c2 = _sync(ROOT/'device_b', silent=True)
        if c1 or c2: resolved.append((i, 'a' if c1 else '', 'b' if c2 else ''))
        [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    return {'iterations': n, 'resolved': len(resolved), 'details': resolved, 'counts': counts, 'match': len(set(counts.values()))==1}

def test_offline_bulk(n=10):
    """Device offline, accumulates many changes, then syncs"""
    setup(); create_file('device_a','seed'); [pull(d) for d in DEVICES]
    # device_c goes offline, a and b keep syncing
    for i in range(n):
        (ROOT/'device_a'/f'online_a_{i}.txt').write_text(f'a{i}')
        sync('device_a', silent=True); pull('device_b')
        (ROOT/'device_b'/f'online_b_{i}.txt').write_text(f'b{i}')
        sync('device_b', silent=True); pull('device_a')
    # device_c makes many offline changes
    for i in range(n):
        (ROOT/'device_c'/f'offline_{i}.txt').write_text(f'c{i}')
    # device_c tries to sync all at once
    ok, conflict = sync('device_c', silent=True)
    [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    return {'online_changes': n*2, 'offline_changes': n, 'conflict': conflict, 'counts': counts, 'match': len(set(counts.values()))==1}

def test_edit_same_file(n=5):
    """Two devices edit same file - both edits preserved via timestamp rename"""
    setup(); fname = create_file('device_a','shared'); [pull(d) for d in DEVICES]
    resolved = []
    for i in range(n):
        # Both edit same file
        (ROOT/'device_a'/fname).write_text(f'edit_a_{i}')
        (ROOT/'device_b'/fname).write_text(f'edit_b_{i}')
        # Both sync with edit detection - should rename to preserve both
        ok1, c1 = sync_edit('device_a', silent=True)
        ok2, c2 = sync_edit('device_b', silent=True)
        if c1 or c2: resolved.append((i, c1, c2))
        [pull(d) for d in DEVICES]
        # Get latest file for next iteration
        files = sorted((ROOT/'device_a').glob('shared_*.txt'))
        fname = files[-1].name if files else fname
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    archive = {d: len(list((ROOT/d/'.archive').glob('*.txt'))) if (ROOT/d/'.archive').exists() else 0 for d in DEVICES}
    return {'iterations': n, 'resolved': len(resolved), 'counts': counts, 'archive': archive, 'match': len(set(counts.values()))==1}

def test_delete_race():
    """Two devices archive same file - should auto-resolve (both want same outcome)"""
    setup(); fname = create_file('device_a','todelete'); [pull(d) for d in DEVICES]
    # Both soft-delete (archive) without pull
    for d in ['device_a', 'device_b']:
        arc = ROOT/d/'.archive'; arc.mkdir(exist_ok=True)
        f = ROOT/d/fname
        if f.exists(): f.rename(arc/fname)
    # Both sync - should auto-resolve
    ok1, c1 = _sync(ROOT/'device_a', silent=True)
    ok2, c2 = _sync(ROOT/'device_b', silent=True)
    [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    archived = {d: len(list((ROOT/d/'.archive').glob('*.txt'))) if (ROOT/d/'.archive').exists() else 0 for d in DEVICES}
    return {'resolved': c1 or c2, 'counts': counts, 'archived': archived, 'match': len(set(counts.values()))==1 and len(set(archived.values()))==1}

def test_edit_delete_race():
    """Device A edits, Device B archives - edit wins (file preserved)"""
    setup(); fname = create_file('device_a','shared'); [pull(d) for d in DEVICES]
    # A edits with sync_edit (renames to new timestamp)
    (ROOT/'device_a'/fname).write_text('edited by a')
    ok1, c1 = sync_edit('device_a', silent=True)
    # B archives (soft delete) the old filename - but file was renamed by A
    arc = ROOT/'device_b'/'.archive'; arc.mkdir(exist_ok=True)
    old_file = ROOT/'device_b'/fname
    if old_file.exists(): old_file.rename(arc/fname)
    ok2, c2 = _sync(ROOT/'device_b', silent=True)
    [pull(d) for d in DEVICES]
    # Edit should win: file should exist (with new timestamp) on all devices
    files = {d: [f.name for f in (ROOT/d).glob('*.txt')] for d in DEVICES}
    archived = {d: [f.name for f in (ROOT/d/'.archive').glob('*.txt')] if (ROOT/d/'.archive').exists() else [] for d in DEVICES}
    edit_preserved = all(len(f) > 0 for f in files.values())
    return {'edit_preserved': edit_preserved, 'files': files, 'archived': archived, 'resolved': c1 or c2}

# === SIM RUNNER ===

TESTS = {
    'race': test_race,
    'offline': test_offline_bulk,
    'edit_same': test_edit_same_file,
    'delete_race': test_delete_race,
    'edit_delete': test_edit_delete_race,
    'old_no_ts': test_old_no_ts,
    'monte': monte_carlo,
}

def sim(name=None, timeout=10):
    """Run a specific test or list all. Usage: sim('race') or sim()"""
    import signal
    if name is None:
        print("Available tests:", ', '.join(TESTS.keys()))
        return
    if name not in TESTS:
        print(f"Unknown test: {name}. Available: {', '.join(TESTS.keys())}")
        return
    def handler(sig, frame): raise TimeoutError(f"Test {name} timed out after {timeout}s")
    signal.signal(signal.SIGALRM, handler)
    signal.alarm(timeout)
    try:
        result = TESTS[name]()
        signal.alarm(0)
        return result
    except TimeoutError as e:
        return {'error': str(e)}

if __name__ == '__main__':
    import sys, json
    if len(sys.argv) > 1:
        print(json.dumps(sim(sys.argv[1]), indent=2))
    else:
        sim()
