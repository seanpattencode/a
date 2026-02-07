"""a work [log|setup|resume #|<N>] - Daily autonomous work agent"""
import sys, os, subprocess as sp, json, time
from pathlib import Path
from ._common import init_db, load_cfg, alog, DATA_DIR, DEVICE_ID, SYNC_ROOT, tm, create_sess

AGENTS_DIR = Path(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))).parent / 'agents'
WORK_LOG = Path(DATA_DIR) / 'work_log.jsonl'

def _entries():
    if not WORK_LOG.exists(): return []
    return [json.loads(l) for l in WORK_LOG.read_text().splitlines() if l.strip()]

def run():
    init_db(); cfg = load_cfg()
    sub = sys.argv[2] if len(sys.argv) > 2 else None

    if sub == 'log':
        from datetime import datetime
        done = [e for e in _entries() if 'end' in e][-15:]
        if not done: print("No work log"); return
        print(f"{'#':<3} {'St':<8} {'Task':<25} {'Start':<12} {'Dur':<6} Session")
        for i, e in enumerate(done):
            st = datetime.fromtimestamp(e['start']).strftime('%m/%d %H:%M')
            dur = f"{int((e['end'] - e['start']) / 60)}m"
            print(f"{i:<3} {e['status']:<8} {e['task'][:25]:<25} {st:<12} {dur:<6} {e['session']}")
        running = [e for e in _entries() if e.get('status') == 'running' and 'end' not in e]
        for e in running:
            alive = tm.has(e['session'])
            print(f"\n{'*' if alive else 'x'} {e['session']} - {e['task']}")
            if alive: print(f"  Resume: tmux attach -t {e['session']}")
        print(f"\nResume: a work resume <#>")
        return

    if sub == 'setup':
        aio = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'a.py')
        sp.run([sys.executable, aio, 'hub', 'add', 'daily-work', '9:00', 'aio', 'work'])
        print("+ Daily work job installed at 9:00am")
        return

    if sub == 'resume':
        n = sys.argv[3] if len(sys.argv) > 3 else None
        if not n or not n.isdigit(): print("Usage: a work resume <#>"); return
        done = [e for e in _entries() if 'end' in e]
        i = int(n)
        if i >= len(done): print(f"x Invalid: {i}"); return
        e = done[i]
        if tm.has(e['session']):
            print(f"Attaching: {e['session']}"); tm.go(e['session']); return
        sn = f"work-resume-{int(time.time())}"
        create_sess(sn, os.getcwd(), 'claude --dangerously-skip-permissions', cfg)
        for _ in range(30):
            time.sleep(1)
            out = sp.run(['tmux', 'capture-pane', '-t', sn, '-p'], capture_output=True, text=True).stdout
            if any(x in out.lower() for x in ['type your message', 'claude', 'opus']): break
        prompt = f"Ultrathink. Resume this task: {e['body']}\n\nPrevious status: {e['status']}\nPrevious output:\n{e.get('output', 'none')[:500]}\n\nWhen done: a done"
        tm.send(sn, prompt); time.sleep(0.3)
        sp.run(['tmux', 'send-keys', '-t', sn, 'Enter'])
        print(f"+ Resumed as {sn}"); tm.go(sn)
        return

    # Default: launch work process in tmux (optional N = task count)
    limit = sub if sub and sub.isdigit() else '3'
    sn = f"work-daily-{int(time.time())}"
    sp.Popen(['tmux', 'new-session', '-d', '-s', sn,
              sys.executable, str(AGENTS_DIR / 'work.py'), limit])
    alog(f"work:launched {sn}")
    print(f"+ Launched: {sn}\n  Monitor: tmux attach -t {sn}\n  Log:     a work log")
