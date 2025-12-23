
import os
import re

new_push_code = """
def cmd_push(args):
    cwd = os.getcwd()
    ensure_git_config()
    res = sp.run(['git', '-C', cwd, 'rev-parse', '--git-dir'], capture_output=True, text=True)
    if res.returncode != 0:
        if '.git/worktrees/' in res.stderr:
            stale = sp.run(f"find {cwd} -name .git -type f 2>/dev/null | xargs grep -l 'worktrees/worktree[0-9]' 2>/dev/null", shell=True, capture_output=True, text=True).stdout.strip().split('\n')
            if any(stale) and _confirm(f"Found {len([x for x in stale if x])} stale .git files. Remove?"):
                for f in [x for x in stale if x]: os.remove(f)
                res = sp.run(['git', '-C', cwd, 'rev-parse', '--git-dir'], capture_output=True, text=True)
        if res.returncode != 0: _die("Not a git repository")
    git_dir = res.stdout.strip()
    is_wt = '.git/worktrees/' in git_dir or cwd.startswith(WORKTREES_DIR)
    targets = [a for a in args if a not in ('--yes','-y')]
    skip = '--yes' in args or '-y' in args
    msg = ' '.join(targets) if targets else f"Update {os.path.basename(cwd)}"
    target_path = targets[0] if targets and os.path.exists(os.path.join(cwd, targets[0])) else None
    if is_wt:
        wt_name = os.path.basename(cwd)
        proj = get_project_for_worktree(cwd)
        if not proj: _die(f"Unknown project for worktree: {wt_name}")
        wt_branch = sp.run(['git', '-C', cwd, 'branch', '--show-current'], capture_output=True, text=True).stdout.strip()
        print(f"\n		📍 Worktree: {wt_name} ({wt_branch}) → {proj}\n   Message: {msg}")
        push_main = True
        if not skip:
            c = input("Push to:\n  1. main (merge & push)\n  2. branch (push only)\nChoice [1]: ").strip()
            if c == '2': push_main = False
            elif c and c != '1': _die("Cancelled", 0)
        sp.run(['git', '-C', cwd, 'add', target_path or '-A'])
        if sp.run(['git', '-C', cwd, 'commit', '-m', msg], capture_output=True).returncode == 0: _ok(f"Committed: {msg}")
        if not push_main:
            if sp.run(['git', '-C', cwd, 'push', '-u', 'origin', wt_branch], env=get_noninteractive_git_env()).returncode != 0: _die("Push failed")
            _ok(f"Pushed to {wt_branch}"); sys.exit(0)
        main = 'main'
        if sp.run(['git', '-C', proj, 'rev-parse', '--verify', 'main'], capture_output=True).returncode != 0: main = 'master'
        print(f" 		→ Switching main to {main}...")
        sp.run(['git', '-C', proj, 'checkout', main], check=True)
        print(f" 		→ Merging {wt_branch}...")
        if sp.run(['git', '-C', proj, 'merge', wt_branch, '--no-edit', '-X', 'theirs'], capture_output=True).returncode != 0: _die("Merge failed")
        _ok("Merged")
        env = get_noninteractive_git_env()
        if sp.run(['git', '-C', proj, 'push', 'origin', main], env=env).returncode != 0:
            if _confirm("Push rejected. Force push?"):
                sp.run(['git', '-C', proj, 'push', '--force', 'origin', main], env=env, check=True)
            else: _die("Push failed")
        _ok(f"Pushed to {main}")
        sp.run(['git', '-C', proj, 'fetch', 'origin'], env=env)
        sp.run(['git', '-C', proj, 'reset', '--hard', f'origin/{main}'])
        if not skip and _confirm(f"Delete worktree '{wt_name}'?"):
            sp.run(['git', '-C', proj, 'worktree', 'remove', '--force', cwd])
            sp.run(['git', '-C', proj, 'branch', '-D', f"wt-{wt_name}"])
            if os.path.exists(cwd): __import__('shutil').rmtree(cwd)
            _ok("Worktree removed")
    else:
        branch = sp.run(['git', '-C', cwd, 'branch', '--show-current'], capture_output=True, text=True).stdout.strip()
        main = 'main'
        if sp.run(['git', '-C', cwd, 'rev-parse', '--verify', 'main'], capture_output=True).returncode != 0: main = 'master'
        sp.run(['git', '-C', cwd, 'add', target_path or '-A'])
        sp.run(['git', '-C', cwd, 'commit', '-m', msg])
        if branch != main:
            sp.run(['git', '-C', cwd, 'checkout', main], check=True)
            sp.run(['git', '-C', cwd, 'merge', branch, '--no-edit', '-X', 'theirs'], check=True)
        env = get_noninteractive_git_env()
        if sp.run(['git', '-C', cwd, 'push', 'origin', main], env=env).returncode != 0:
             if _confirm("Push rejected. Force push?"):
                sp.run(['git', '-C', cwd, 'push', '--force', 'origin', main], env=env, check=True)
             else: _die("Push failed")
        _ok(f"Pushed to {main}")
"""

with open('aio.py', 'r') as f:
    content = f.read()

insert_point = content.find("def cmd_pull")
content = content[:insert_point] + new_push_code + "\n" + content[insert_point:]

pattern = re.compile(r"elif arg == 'push':\n.*?elif arg == 'pull':", re.DOTALL)
content = pattern.sub("elif arg == 'push':\n    cmd_push(sys.argv[2:])\nelif arg == 'pull':", content)

with open('aio.py', 'w') as f:
    f.write(content)
print("Refactored push command")
