"""Fast sync test using pygit2 (no subprocess)"""
import pygit2, time, random, shutil
from pathlib import Path

ROOT, DEVICES = Path(__file__).parent / 'devices', ('device_a', 'device_b', 'device_c')

def ts(): return time.strftime('%Y%m%dT%H%M%S') + f'.{time.time_ns() % 1000000000:09d}'

def setup():
    shutil.rmtree(ROOT, ignore_errors=True)
    ROOT.mkdir(parents=True)
    # Create bare origin
    pygit2.init_repository(str(ROOT/'origin'), bare=True)
    # Create device repos
    for d in DEVICES:
        repo = pygit2.init_repository(str(ROOT/d))
        repo.remotes.create('origin', str(ROOT/'origin'))
        # Initial commit so we have a branch
        (ROOT/d/'seed.txt').write_text('init')
        repo.index.add('seed.txt')
        repo.index.write()
        tree = repo.index.write_tree()
        sig = pygit2.Signature('test', 'test@test.com')
        repo.create_commit('HEAD', sig, sig, 'init', tree, [])
        # Push to origin
        repo.remotes['origin'].push(['refs/heads/master:refs/heads/master'])

def get_repo(device): return pygit2.Repository(str(ROOT/device))

def commit_all(repo, msg='sync'):
    """Stage all changes and commit"""
    repo.index.read()
    repo.index.add_all()
    # Also handle deletions
    for entry in list(repo.index):
        if not (Path(repo.workdir) / entry.path).exists():
            repo.index.remove(entry.path)
    repo.index.write()
    tree = repo.index.write_tree()
    sig = pygit2.Signature('test', 'test@test.com')
    try:
        parent = repo.head.peel().id
        parents = [parent]
    except:
        parents = []
    return repo.create_commit('HEAD', sig, sig, msg, tree, parents)

def pull(repo):
    """Fetch and merge origin/master"""
    try:
        repo.remotes['origin'].fetch()
        remote_ref = repo.lookup_reference('refs/remotes/origin/master')
        if not remote_ref: return
        remote_commit = repo.get(remote_ref.target)
        local_ref = repo.lookup_reference('refs/heads/master')
        local_commit = repo.get(local_ref.target)
        # Merge
        merge_result, _ = repo.merge_analysis(remote_ref.target)
        if merge_result & pygit2.GIT_MERGE_ANALYSIS_UP_TO_DATE:
            return
        if merge_result & pygit2.GIT_MERGE_ANALYSIS_FASTFORWARD:
            local_ref.set_target(remote_ref.target)
            repo.head.set_target(remote_ref.target)
            repo.checkout_head(strategy=pygit2.GIT_CHECKOUT_FORCE)
        elif merge_result & pygit2.GIT_MERGE_ANALYSIS_NORMAL:
            repo.merge(remote_ref.target)
            if repo.index.conflicts:
                # Auto-resolve: accept theirs (edit wins)
                for conflict in list(repo.index.conflicts):
                    if conflict[2]:  # theirs exists
                        repo.index.add(conflict[2].path)
                    repo.index.conflicts.remove(conflict[0].path if conflict[0] else conflict[1].path if conflict[1] else conflict[2].path)
            commit_all(repo, 'merge')
            repo.state_cleanup()
    except Exception: pass

def push(repo):
    """Push to origin"""
    try:
        repo.remotes['origin'].push(['refs/heads/master:refs/heads/master'])
        return True
    except pygit2.GitError as e:
        return False

def sync(device):
    """Full sync: commit, pull, push with retry"""
    repo = get_repo(device)
    commit_all(repo)
    for _ in range(3):
        pull(repo)
        if push(repo): return True, False
    return False, True  # conflict

def create_file(device, name, content=''):
    p = ROOT/device/f'{name}_{ts()}.txt'
    p.write_text(content or f'by {device}')
    ok, _ = sync(device)
    return p.name

def soft_delete(device, name):
    """Archive instead of delete"""
    arc = ROOT/device/'.archive'
    arc.mkdir(exist_ok=True)
    for p in (ROOT/device).glob(f'{name}*.txt'):
        p.rename(arc/p.name)
    return sync(device)

def monte_carlo(n=1000, verbose=False):
    setup()
    create_file('device_a', 'seed')
    for d in DEVICES: pull(get_repo(d))

    conflicts, errors, reseeds = [], [], 0
    ops = ['add', 'delete', 'edit', 'archive']

    for i in range(n):
        d = random.choice(DEVICES)
        op = random.choice(ops)

        try:
            pull(get_repo(d))
            files = list((ROOT/d).glob('*.txt'))

            if not files:
                (ROOT/d/f'reseed_{ts()}.txt').write_text('reseed')
                sync(d)
                reseeds += 1
                continue

            if op == 'add':
                (ROOT/d/f'f{i}_{ts()}.txt').write_text(f'{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'add'))
            elif op == 'delete' and files:
                arc = ROOT/d/'.archive'; arc.mkdir(exist_ok=True)
                f = random.choice(files); f.rename(arc/f.name)
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'delete'))
            elif op == 'edit' and files:
                random.choice(files).write_text(f'edit{i}')
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'edit'))
            elif op == 'archive' and files:
                arc = ROOT/d/'.archive'; arc.mkdir(exist_ok=True)
                f = random.choice(files); f.rename(arc/f.name)
                ok, c = sync(d)
                if c: conflicts.append((i, d, 'archive'))

        except Exception as e:
            errors.append((i, d, op, str(e)))
            if verbose: print(f"[{i}] {d} {op}: {e}")

    for d in DEVICES: pull(get_repo(d))
    counts = {d: len(list((ROOT/d).glob('*.txt'))) for d in DEVICES}

    return {
        'actions': n,
        'errors': len(errors),
        'conflicts': len(conflicts),
        'reseeds': reseeds,
        'counts': counts,
        'match': len(set(counts.values())) == 1,
        'error_details': errors[:5] if errors else []
    }

def sim(name=None, timeout=10):
    """Run test with timeout"""
    import signal
    tests = {'monte': monte_carlo}
    if name is None:
        print("Available:", ', '.join(tests.keys()))
        return
    if name not in tests:
        print(f"Unknown: {name}")
        return
    def handler(sig, frame): raise TimeoutError(f"Timeout {timeout}s")
    signal.signal(signal.SIGALRM, handler)
    signal.alarm(timeout)
    try:
        result = tests[name]()
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
