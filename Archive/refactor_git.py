
import os
import re

new_funcs = r'''
def cmd_pull(force=False):
    cwd = os.getcwd()
    if sp.run(['git', '-C', cwd, 'rev-parse', '--git-dir'], capture_output=True).returncode != 0: _die("Not a git repository")
    env = get_noninteractive_git_env()
    if sp.run(['git', '-C', cwd, 'fetch', 'origin'], env=env).returncode != 0: _die("Fetch failed (check auth/remote)")
    
    ref = 'origin/main'
    if sp.run(['git', '-C', cwd, 'rev-parse', '--verify', ref], capture_output=True).returncode != 0: ref = 'origin/master'
    
    print("⚠ WARNING: resetting to server version!")
    if not force and not _confirm("Are you sure?"): _die("Cancelled", 0) 
    
    sp.run(['git', '-C', cwd, 'reset', '--hard', ref])
    sp.run(['git', '-C', cwd, 'clean', '-f', '-d'])
    _ok("Synced with server")

def cmd_revert(num_str):
    cwd = os.getcwd()
    if sp.run(['git', '-C', cwd, 'rev-parse', '--git-dir'], capture_output=True).returncode != 0: _die("Not a git repository")
    num = int(num_str) if num_str and num_str.isdigit() else 1
    range_spec = 'HEAD' if num == 1 else f'HEAD~{num}..HEAD'
    if sp.run(['git', '-C', cwd, 'revert', range_spec, '--no-edit']).returncode == 0:
        _ok(f"Reverted last {num} commits")
    else:
        _die("Revert failed")
'''

with open('aio.py', 'r') as f:
    content = f.read()

# Insert functions after format_app_command
insert_point = content.find("def format_app_command")
if insert_point != -1:
    # Find end of that function
    # It ends when indentation returns to 0 or next def
    # Let's just insert before get_noninteractive_git_env
    insert_point = content.find("def get_noninteractive_git_env")
    content = content[:insert_point] + new_funcs + "\n" + content[insert_point:]

# Replace pull block
# elif arg == 'pull': ... elif arg == 'revert':
pull_pattern = re.compile(r"elif arg == 'pull':\n.*?elif arg == 'revert':", re.DOTALL)
content = pull_pattern.sub("elif arg == 'pull':\n    cmd_pull('--yes' in sys.argv or '-y' in sys.argv)\nelif arg == 'revert':", content)

# Replace revert block
# elif arg == 'revert': ... elif arg == 'setup':
revert_pattern = re.compile(r"elif arg == 'revert':\n.*?elif arg == 'setup':", re.DOTALL)
content = revert_pattern.sub("elif arg == 'revert':\n    cmd_revert(work_dir_arg)\nelif arg == 'setup':", content)

with open('aio.py', 'w') as f:
    f.write(content)

print("Refactored git commands")
