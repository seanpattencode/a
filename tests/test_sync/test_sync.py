"""
Sync test with production-ready logic (subprocess-based).
Core functions can be copy-pasted to a_cmd/sync.py.
"""
import subprocess as sp, time, shlex, random
from pathlib import Path

# === TEST HARNESS (not for production) ===
ROOT, DEVICES = Path(__file__).parent / 'devices', ('device_a', 'device_b', 'device_c')

def setup():
    sp.run(f'rm -rf {q(ROOT)} && mkdir -p {q(ROOT/"origin")} && git -C {q(ROOT/"origin")} init -q -b main --bare', shell=True)
    for d in DEVICES:
        sp.run(f'mkdir -p {q(ROOT/d)} && git -C {q(ROOT/d)} init -q -b main && git -C {q(ROOT/d)} remote add origin {q(ROOT/"origin")}', shell=True)

# === PRODUCTION-READY FUNCTIONS (copy to sync.py) ===

MAX_RETRIES = 3

def q(p):
    """Quote path for shell"""
    return shlex.quote(str(p))

def ts():
    """Generate timestamp with nanosecond precision"""
    return time.strftime('%Y%m%dT%H%M%S') + f'.{time.time_ns() % 1000000000:09d}'

def is_conflict(text):
    """Detect all git conflict/error types"""
    t = text.lower()
    return any(x in t for x in ['conflict', 'diverged', 'rejected', 'overwritten', 'unmerged', 'aborting'])

def add_timestamps(path, recursive=False):
    """Add timestamps to any files missing them"""
    timestamp = ts()
    pattern = '**/*.txt' if recursive else '*.txt'
    for p in Path(path).glob(pattern):
        if '_20' in p.stem or p.name.startswith('.'):
            continue
        p.rename(p.with_name(f'{p.stem}_{timestamp}{p.suffix}'))

def resolve_conflicts(path):
    """Auto-resolve conflicts: edit wins (accept theirs)"""
    p = q(path)
    # Get list of unmerged files
    r = sp.run(f'cd {p} && git diff --name-only --diff-filter=U', shell=True, capture_output=True, text=True)
    unmerged = [f for f in r.stdout.strip().split('\n') if f]
    for f in unmerged:
        # Edit wins: accept theirs (remote has edits), fallback to ours, fallback to remove
        sp.run(f'cd {p} && git checkout --theirs {shlex.quote(f)} 2>/dev/null || git checkout --ours {shlex.quote(f)} 2>/dev/null || git rm -f {shlex.quote(f)} 2>/dev/null', shell=True, capture_output=True)
    # Accept all incoming changes for any remaining conflicts
    sp.run(f'cd {p} && git checkout --theirs . 2>/dev/null', shell=True, capture_output=True)
    sp.run(f'cd {p} && git add -A', shell=True, capture_output=True)

def _sync(path, silent=False, folders=None):
    """
    Sync with auto-resolution. Returns (success, had_conflict).

    - Retries up to MAX_RETRIES times on push rejection
    - Auto-resolves merge conflicts (edit wins)
    - Timestamps files before sync to prevent filename collisions
    """
    p = q(path)
    had_conflict = False

    # Auto-timestamp files in specified folders (or root if none)
    if folders:
        for f in folders:
            folder_path = Path(path) / f
            if folder_path.exists():
                add_timestamps(folder_path)

    for attempt in range(MAX_RETRIES):
        # Step 1: Commit local changes first (prevents "overwritten" errors)
        sp.run(f'cd {p} && git add -A && git commit -qm sync', shell=True, capture_output=True)

        # Step 2: Pull with merge (not rebase)
        pull = sp.run(f'cd {p} && git pull --no-rebase origin main', shell=True, capture_output=True, text=True)

        if is_conflict(pull.stderr + pull.stdout):
            had_conflict = True
            resolve_conflicts(path)
            sp.run(f'cd {p} && git commit -qm "auto-resolve: edit wins"', shell=True, capture_output=True)

        # Step 3: Push
        push = sp.run(f'cd {p} && git push -q origin main', shell=True, capture_output=True, text=True)

        if push.returncode == 0:
            return True, had_conflict

        # Retry if rejected (remote has newer commits)
        if 'rejected' in (push.stderr + push.stdout).lower():
            had_conflict = True
            continue

        # Other errors: fail
        if not silent:
            print(f"Sync error: {(push.stderr + push.stdout)[:200]}")
        return False, had_conflict

    if not silent:
        print(f"Sync failed after {MAX_RETRIES} retries")
    return False, had_conflict

def soft_delete(path, filepath):
    """Archive instead of hard delete (prevents edit vs delete conflicts)"""
    arc = Path(path) / '.archive'
    arc.mkdir(exist_ok=True)
    f = Path(filepath)
    if f.exists():
        f.rename(arc / f.name)

# === TEST HELPERS ===

def pull(device):
    """Pull with auto-commit and conflict resolution"""
    d = ROOT / device
    p = q(d)
    sp.run(f'cd {p} && git add -A && git commit -qm "pre-pull"', shell=True, capture_output=True)
    r = sp.run(f'cd {p} && git pull --no-rebase origin main', shell=True, capture_output=True, text=True)
    if is_conflict(r.stderr + r.stdout):
        resolve_conflicts(d)
        sp.run(f'cd {p} && git commit -qm "auto-resolve"', shell=True, capture_output=True)

def create_file(device, name, content=''):
    p = ROOT / device / f'{name}_{ts()}.txt'
    p.write_text(content or f'created by {device}')
    sp.run(f'cd {q(ROOT/device)} && git add -A && git commit -qm "add {p.name}" && git push -u origin main', shell=True, capture_output=True)
    return p.name

def sync(device, silent=True):
    return _sync(ROOT / device, silent=silent)

def sync_edit(device, silent=True):
    """Sync with edit detection: rename modified files to new timestamp"""
    d = ROOT / device
    t = ts()
    arc = d / '.archive'
    arc.mkdir(exist_ok=True)
    # Find modified files and rename them (preserves both versions)
    for p in d.glob('*.txt'):
        r = sp.run(f'git -C {q(d)} diff --quiet {shlex.quote(p.name)}', shell=True)
        if r.returncode:  # file was modified
            # Archive old version from git
            old = sp.run(f'git -C {q(d)} show HEAD:{shlex.quote(p.name)}', shell=True, capture_output=True, text=True)
            if old.stdout:
                (arc / p.name).write_text(old.stdout)
            # Rename to new timestamp
            base = p.stem.rsplit('_', 1)[0] if '_20' in p.stem else p.stem
            p.rename(p.with_name(f'{base}_{t}{p.suffix}'))
    return _sync(d, silent=silent)

# === CONFLICT TESTS ===

def test_race(n=5):
    """Two devices push without pull - auto-resolved via retry"""
    setup(); create_file('device_a', 'seed'); [pull(d) for d in DEVICES]
    resolved = []
    for i in range(n):
        (ROOT/'device_a'/f'race_a_{i}.txt').write_text(f'a{i}')
        (ROOT/'device_b'/f'race_b_{i}.txt').write_text(f'b{i}')
        ok1, c1 = sync('device_a')
        ok2, c2 = sync('device_b')
        if c1 or c2: resolved.append((i, 'a' if c1 else '', 'b' if c2 else ''))
        [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    return {'iterations': n, 'resolved': len(resolved), 'counts': counts, 'match': len(set(counts.values()))==1}

def test_offline_bulk(n=10):
    """Device offline, accumulates changes, then syncs"""
    setup(); create_file('device_a', 'seed'); [pull(d) for d in DEVICES]
    for i in range(n):
        (ROOT/'device_a'/f'online_a_{i}.txt').write_text(f'a{i}')
        sync('device_a'); pull('device_b')
        (ROOT/'device_b'/f'online_b_{i}.txt').write_text(f'b{i}')
        sync('device_b'); pull('device_a')
    for i in range(n):
        (ROOT/'device_c'/f'offline_{i}.txt').write_text(f'c{i}')
    ok, conflict = sync('device_c')
    [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    return {'online': n*2, 'offline': n, 'conflict': conflict, 'counts': counts, 'match': len(set(counts.values()))==1}

def test_edit_same_file(n=5):
    """Two devices edit same file - both preserved via timestamp rename"""
    setup(); fname = create_file('device_a', 'shared'); [pull(d) for d in DEVICES]
    resolved = []
    for i in range(n):
        (ROOT/'device_a'/fname).write_text(f'edit_a_{i}')
        (ROOT/'device_b'/fname).write_text(f'edit_b_{i}')
        ok1, c1 = sync_edit('device_a')
        ok2, c2 = sync_edit('device_b')
        if c1 or c2: resolved.append((i, c1, c2))
        [pull(d) for d in DEVICES]
        files = sorted((ROOT/'device_a').glob('shared_*.txt'))
        fname = files[-1].name if files else fname
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    archive = {d: len(list((ROOT/d/'.archive').glob('*.txt'))) if (ROOT/d/'.archive').exists() else 0 for d in DEVICES}
    return {'iterations': n, 'resolved': len(resolved), 'counts': counts, 'archive': archive, 'match': len(set(counts.values()))==1}

def test_delete_race():
    """Two devices archive same file - both want same outcome"""
    setup(); fname = create_file('device_a', 'todelete'); [pull(d) for d in DEVICES]
    for d in ['device_a', 'device_b']:
        soft_delete(ROOT/d, ROOT/d/fname)
    ok1, c1 = sync('device_a')
    ok2, c2 = sync('device_b')
    [pull(d) for d in DEVICES]
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}
    archived = {d: len(list((ROOT/d/'.archive').glob('*.txt'))) if (ROOT/d/'.archive').exists() else 0 for d in DEVICES}
    return {'resolved': c1 or c2, 'counts': counts, 'archived': archived, 'match': len(set(counts.values()))==1}

def test_edit_delete_race():
    """Device A edits, Device B archives - edit wins"""
    setup(); fname = create_file('device_a', 'shared'); [pull(d) for d in DEVICES]
    (ROOT/'device_a'/fname).write_text('edited by a')
    ok1, c1 = sync_edit('device_a')
    soft_delete(ROOT/'device_b', ROOT/'device_b'/fname)
    ok2, c2 = sync('device_b')
    [pull(d) for d in DEVICES]
    files = {d: [f.name for f in (ROOT/d).glob('*.txt')] for d in DEVICES}
    edit_preserved = all(len(f) > 0 for f in files.values())
    return {'edit_preserved': edit_preserved, 'files': files, 'resolved': c1 or c2}

# === MONTE CARLO ===

def monte_carlo(n=1000, verbose=False):
    setup(); create_file('device_a', 'seed'); [pull(d) for d in DEVICES]
    for d in DEVICES:
        (ROOT/d/'nested').mkdir(exist_ok=True)
        sp.run(f'cd {q(ROOT/d)} && git add -A && git commit -qm "mkdir" && git push origin main', shell=True, capture_output=True)
    [pull(d) for d in DEVICES]

    conflicts, errors, reseeds = [], [], 0
    ops = ['add', 'delete', 'archive', 'edit', 'edit_raw', 'nested', 'non_txt']

    for i in range(n):
        d = random.choice(DEVICES)
        op = random.choice(ops)

        try:
            pull(d)
            files = list((ROOT/d).glob('*.txt'))

            if not files:
                if verbose: print(f"[{i}] reseed {d}")
                (ROOT/d/f'reseed_{i}.txt').write_text(f'reseed {i}')
                sync(d)
                reseeds += 1
                continue

            if op == 'add':
                (ROOT/d/f'f{i}.txt').write_text(f'{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'add'))
            elif op == 'delete' and files:
                soft_delete(ROOT/d, random.choice(files))
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'delete'))
            elif op == 'archive' and files:
                soft_delete(ROOT/d, random.choice(files))
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'archive'))
            elif op == 'edit' and files:
                random.choice(files).write_text(f'edit{i}')
                ok, c = sync_edit(d)
                if c: conflicts.append((i, d, 'edit'))
            elif op == 'edit_raw' and files:
                random.choice(files).write_text(f'raw{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'edit_raw'))
            elif op == 'nested':
                (ROOT/d/'nested'/f'n{i}.txt').write_text(f'{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'nested'))
            elif op == 'non_txt':
                ext = random.choice(['.json', '.md', '.yaml'])
                (ROOT/d/f'file{i}{ext}').write_text(f'{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'non_txt'))

        except Exception as e:
            errors.append((i, d, op, str(e)))
            if verbose: print(f"[{i}] {d} {op}: {e}")

    [pull(d) for d in DEVICES]
    counts = {d: (
        len(list((ROOT/d).glob('*.txt'))),
        len(list((ROOT/d/'.archive').glob('*.txt'))) if (ROOT/d/'.archive').exists() else 0,
        len(list((ROOT/d/'nested').glob('*.txt'))) if (ROOT/d/'nested').exists() else 0,
    ) for d in DEVICES}
    by_op = {op: len([c for c in conflicts if c[2]==op]) for op in ops}

    return {
        'actions': n,
        'errors': len(errors),
        'conflicts': len(conflicts),
        'by_op': {k:v for k,v in by_op.items() if v},
        'reseeds': reseeds,
        'counts': counts,
        'match': len(set(counts.values())) == 1,
        'error_details': errors[:5] if errors else []
    }

# === TEST RUNNER ===

TESTS = {
    'race': test_race,
    'offline': test_offline_bulk,
    'edit_same': test_edit_same_file,
    'delete_race': test_delete_race,
    'edit_delete': test_edit_delete_race,
    'monte': monte_carlo,
}

def sim(name=None, timeout=10):
    """Run a test with timeout. Usage: sim('race') or sim()"""
    import signal
    if name is None:
        print("Available:", ', '.join(TESTS.keys()))
        return
    if name not in TESTS:
        print(f"Unknown: {name}. Available: {', '.join(TESTS.keys())}")
        return
    def handler(sig, frame): raise TimeoutError(f"Timeout {timeout}s")
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
        print(json.dumps(sim(sys.argv[1], timeout=60), indent=2))
    else:
        sim()
