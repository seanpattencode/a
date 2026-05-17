"""a bg <command...> — split a pane in the caller's current tmux window (fallback: new window in session 'a'),
tee'd to log, done via tmux wait-for. Many watchers can `tmux wait-for d-<name>` and unblock simultaneously.
Memory-risky jobs:
  Linux: a bg systemd-run --user --scope --property=MemoryMax=8G --property=MemorySwapMax=0 ./job
  macOS: ulimit -v 8000000; ./job"""
import sys, os, subprocess as sp, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import ADATA_ROOT

LOGS = str(ADATA_ROOT / 'local' / 'bg')

def run():
    args = sys.argv[2:]
    if not args:
        sp.run(['tmux','list-panes','-F','#{pane_id} #{pane_start_command}'])
        print(f'\nlogs: {LOGS}/\nUsage: a bg <command...>')
        return
    os.makedirs(LOGS, exist_ok=True)
    cmd = 'a ' + ' '.join(args)
    name = f'{args[0].replace("/","_")}-{time.strftime("%H%M")}'
    log = f'{LOGS}/{name}.log'
    sig = f'd-{name}'
    banner = f"=== a bg: {name} ===\\nstart: {time.strftime('%Y-%m-%d %H:%M:%S')}\\ncmd:   {cmd}\\nlog:   {log}\\n--- starting ---"
    wrap = f"printf '{banner}\\n' | tee {log}; PYTHONUNBUFFERED=1 stdbuf -oL -eL {cmd} 2>&1 | tee -a {log}; echo; echo '=== DONE ==='; tmux wait-for -S {sig}; read"
    if os.environ.get('TMUX'):
        sp.run(['tmux','split-window','-d',wrap],check=True)
        sp.run(['tmux','select-layout','tiled'],stderr=sp.DEVNULL)
    else:
        sp.run(['tmux','new-window','-d','-t','a:','-n',name,wrap],check=True)
    print(f'+ pane [{name}] log: {log}')
    sp.run(['tmux','wait-for',sig])
    print('=== DONE ===')

run()
