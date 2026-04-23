"""Shared utilities for aio commands"""
import sys, os, subprocess as sp, json, shutil, time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
ADATA_ROOT = next((p for p in [Path(SCRIPT_DIR)/'adata', Path.home()/'a'/'adata', Path.home()/'adata'] if (p/'git').exists()), Path(SCRIPT_DIR)/'adata')
DATA_DIR, SYNC_ROOT = str(ADATA_ROOT/'local'), ADATA_ROOT/'git'
PROMPTS_DIR = ADATA_ROOT/'git'/'common'/'prompts'
RCLONE_REMOTE_PREFIX, RCLONE_BACKUP_PATH = 'a-gdrive', 'adata'
ACTIVITY_DIR = ADATA_ROOT/'git'/'activity'
def _get_dev():
    f = os.path.join(DATA_DIR, '.device')
    if os.path.exists(f): return open(f).read().strip()
    import socket; d = (sp.run(['getprop','ro.product.model'],capture_output=True,text=True).stdout.strip().replace(' ','-') or socket.gethostname()) if os.path.exists('/data/data/com.termux') else socket.gethostname()
    os.makedirs(os.path.dirname(f), exist_ok=True); open(f,'w').write(d); return d
DEVICE_ID = _get_dev()

def alog(msg):
    ACTIVITY_DIR.mkdir(parents=True, exist_ok=True); now = datetime.now(); cwd = os.getcwd()
    r = sp.run(['git','remote','get-url','origin'], capture_output=True, text=True, cwd=cwd) if os.path.isdir(cwd+'/.git') else None
    repo = f' git:{r.stdout.strip()}' if r and r.returncode == 0 and r.stdout.strip() else ''
    (ACTIVITY_DIR/now.strftime(f'%Y%m%dT%H%M%S.{int(now.timestamp()*1000)%1000:03d}_{DEVICE_ID}.txt')).write_text(f'{now:%m/%d %H:%M} {DEVICE_ID} {msg} {cwd}{repo}\n')

def _git(path, *a, **k): return sp.run(['git', '-C', path] + list(a), capture_output=True, text=True, **k)
def _die(m, c=1): print(f"x {m}"); sys.exit(c)
def _sg(*a, **k): return sp.run(['git', '-C', SCRIPT_DIR] + list(a), capture_output=True, text=True, **k)
def _env():
    e = os.environ.copy(); e.pop('DISPLAY', None); e.pop('GPG_AGENT_INFO', None); e['GIT_TERMINAL_PROMPT'] = '0'
    return e

def _tx(*a,**k): return sp.run(['tmux']+list(a), capture_output=True, text=True, **k)
class TM:
    def new(s,n,d,c,e=None): return sp.run(['tmux','new-session','-d','-s',n,'-c',d]+([c]if c else[]),capture_output=True,env=e)
    def send(s,n,t): return sp.run(['tmux','send-keys','-l','-t',n,t])
    def go(s,n): os.execvp('tmux',['tmux','switch-client','-t',n] if'TMUX'in os.environ else['tmux','new-session','-t',n])
    def has(s,n):
        try: return _tx('has-session','-t',n,timeout=2).returncode==0
        except sp.TimeoutExpired: return False
    def ls(s): return _tx('list-sessions','-F','#{session_name}')
tm = TM()

def get_prompt(name):
    pf = PROMPTS_DIR / f'{name}.txt'
    return pf.read_text(errors='replace').strip() if pf.exists() else None

def jl_append(name, rec):
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(os.path.join(DATA_DIR, f'{name}.jsonl'), 'a') as f: f.write(json.dumps(rec)+'\n')
def jl_read(name):
    p = os.path.join(DATA_DIR, f'{name}.jsonl')
    return [json.loads(l) for l in open(p)] if os.path.exists(p) else []
def jl_clear(name):
    p = os.path.join(DATA_DIR, f'{name}.jsonl')
    os.path.exists(p) and os.remove(p)
def init_db(): os.makedirs(DATA_DIR, exist_ok=True)
def cfset(k, v):
    cfg = load_cfg(); cfg[k] = v
    p = SYNC_ROOT/'workspace'/'config.txt'; p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(''.join(f'{kk}: {vv.replace(chr(10), chr(92)+"n") if isinstance(vv,str) else vv}\n' for kk,vv in cfg.items()))

def load_cfg():
    p = SYNC_ROOT/'workspace'/'config.txt'
    if not p.exists(): return {}
    return {k.strip(): v.strip().replace('\\n', '\n') for line in p.read_text().splitlines() if ':' in line for k, v in [line.split(':', 1)]}

def _kvdir(sub):
    d = SYNC_ROOT/'workspace'/sub; d.mkdir(parents=True, exist_ok=True)
    return [{k.strip():v.strip() for line in f.read_text().splitlines() if ':' in line for k,v in[line.split(':',1)]} for f in d.glob('*.txt')]
def load_proj():
    return [(os.path.expanduser(d.get('Path',f'~/{d["Name"]}')),d.get('Repo','')) for d in sorted(_kvdir('projects'),key=lambda x:x.get('Name','')) if 'Name' in d]
def load_apps():
    return sorted([(d['Name'],d['Command']) for d in _kvdir('cmds') if 'Name' in d and 'Command' in d])

def load_sess(cfg):
    p = SYNC_ROOT/'workspace'/'sessions.txt'
    data = [r for line in (p.read_text().splitlines() if p.exists() else []) if '|' in line for r in [line.split('|', 2)] if len(r) == 3]
    dp, s = get_prompt('default'), {}
    esc = lambda p: cfg.get(p, dp or '').replace('\n', '\\n').replace('"', '\\"')
    for k, n, t in data:
        s[k] = (n, t.replace(' "{CLAUDE_PROMPT}"', '').replace(' "{CODEX_PROMPT}"', '').replace(' "{GEMINI_PROMPT}"', '') if k in ['cp','lp','gp'] else t.format(CLAUDE_PROMPT=esc('claude_prompt'), CODEX_PROMPT=esc('codex_prompt'), GEMINI_PROMPT=esc('gemini_prompt')))
    return s

# Cloud
def get_rclone(): return shutil.which('rclone') or next((p for p in ['/usr/bin/rclone', os.path.expanduser('~/.local/bin/rclone')] if os.path.isfile(p)), None)
def _configured_remotes():
    if not (rc := get_rclone()): return []
    r = sp.run([rc, 'listremotes'], capture_output=True, text=True)
    return [l.rstrip(':') for l in r.stdout.splitlines() if l.rstrip(':').startswith(RCLONE_REMOTE_PREFIX)] if r.returncode == 0 else []
def cloud_sync(wait=False):
    rc, remotes = get_rclone(), _configured_remotes()
    if not rc or not remotes: return False, None
    def _sync():
        ok = True
        for rem in remotes:
            r = sp.run([rc, 'copy', DATA_DIR, f'{rem}:{RCLONE_BACKUP_PATH}/backup/data', '-q', '--exclude', '*.db*', '--exclude', '*cache*', '--exclude', 'timing.jsonl', '--exclude', '.device', '--exclude', '.git/**', '--exclude', 'logs/**'], capture_output=True, text=True)
            for f in ['~/.config/gh/hosts.yml', '~/.config/rclone/rclone.conf']:
                p = os.path.expanduser(f); os.path.exists(p) and sp.run([rc, 'copy', p, f'{rem}:{RCLONE_BACKUP_PATH}/backup/auth/', '-q'], capture_output=True)
            ok = ok and r.returncode == 0
        Path(DATA_DIR, '.gdrive_sync').touch() if ok else None; return ok
    return (True, _sync()) if wait else (__import__('threading').Thread(target=_sync, daemon=True).start(), (True, None))[1]
def _cloud_install():
    u=os.uname();s,bd,arch='osx'if u.sysname=='Darwin'else'linux',os.path.expanduser('~/.local/bin'),'amd64'if u.machine in('x86_64','AMD64')else'arm64'
    if sp.run(f'curl -sL https://downloads.rclone.org/rclone-current-{s}-{arch}.zip -o /tmp/rclone.zip && unzip -qjo /tmp/rclone.zip "*/rclone" -d {bd} && chmod +x {bd}/rclone', shell=True).returncode == 0:
        return f'{bd}/rclone'
    return None
def cloud_login(remote=None, custom=False):
    rc = get_rclone() or _cloud_install()
    if not rc: print("x rclone install failed"); return False
    existing = _configured_remotes()
    rem = remote or (RCLONE_REMOTE_PREFIX if RCLONE_REMOTE_PREFIX not in existing else f'{RCLONE_REMOTE_PREFIX}{len(existing)+1}')
    cmd = [rc, 'config', 'create', rem, 'drive']
    if custom:
        cid = input("client_id: ").strip(); csec = input("client_secret: ").strip()
        if not cid or not csec: print("x Both required"); return False
        cmd += ['client_id', cid, 'client_secret', csec]
    sp.run(cmd)
    if rem not in _configured_remotes(): print("x Login failed"); return False
    cloud_sync(wait=True); return True
def cloud_logout(remote=None):
    remotes = _configured_remotes()
    if not remotes: print("Not logged in"); return False
    sp.run([get_rclone(), 'config', 'delete', remote or remotes[-1]]); return True
def cloud_status():
    remotes = _configured_remotes()
    if not remotes: print("x Not logged in\n\nSetup: a gdrive login"); return False
    for rem in remotes: print(f"  {rem}")
    return True

# Worktrees
def _wt_items(wt_dir): return sorted([d for d in os.listdir(wt_dir) if os.path.isdir(os.path.join(wt_dir, d))]) if os.path.exists(wt_dir) else []

LOG_DIR = str(ADATA_ROOT/'backup'/DEVICE_ID)
def ensure_tmux(cfg):
    if cfg.get('tmux_conf')=='y' and not sp.run(['tmux','info'],stdout=sp.DEVNULL,stderr=sp.DEVNULL).returncode: sp.run(['tmux','refresh-client','-S'],capture_output=True)
def create_sess(sn, wd, cmd, cfg, env=None, skip_prefix=False):
    if cmd and 'claude ' in cmd and '--effort' not in cmd: cmd = cmd.replace('claude ', 'claude --effort max ', 1)
    ai = cmd and any(a in cmd for a in ['codex','claude','gemini','aider'])
    if ai: cmd = f'while :;do {cmd};e=$?;[ $e -eq 0 ]&&break;echo -e "\\n! Crashed ($e). [R]estart/[Q]uit: ";read -n1 k;[[ $k =~ [Rr] ]]||break;done'
    r = tm.new(sn, wd, cmd or '', env); ensure_tmux(cfg)
    if ai: sp.run(['tmux','split-window','-v']+(['-p','40']if os.environ.get('TERMUX_VERSION')else[])+['-t',sn,'-c',wd,'sh -c "ls;exec $SHELL"'],capture_output=True); sp.run(['tmux','select-pane','-t',sn,'-U'],capture_output=True)
    os.makedirs(LOG_DIR, exist_ok=True); sp.run(['tmux','pipe-pane','-t',sn,f'cat >> {LOG_DIR}/{DEVICE_ID}__{sn}.log'],capture_output=True)
    jl_append('agent_logs', {'session': sn, 'started': time.time(), 'device': DEVICE_ID})
    return r

def get_prefix(agent, cfg, wd=None):
    dp = get_prompt('default') or ''
    pre = cfg.get('claude_prefix', 'Ultrathink. ') if 'claude' in agent else ''
    af = Path(wd or os.getcwd()) / 'AGENTS.md'
    return (dp + ' ' if dp else '') + pre + (af.read_text().strip() + ' ' if af.exists() else '')

def send_prefix(sn, agent, wd, cfg, prompt=None):
    if prompt:
        script = f'import subprocess as s\ns.run(["tmux","wait-for","rdy-{sn}"]);s.run(["tmux","send-keys","-l","-t","{sn}",{repr(prompt)}]);s.run(["tmux","send-keys","-t","{sn}","Enter"])'
    else:
        pre = get_prefix(agent, cfg, wd)
        if not pre: return
        script = f'import subprocess as s\ns.run(["tmux","wait-for","rdy-{sn}"]);s.run(["tmux","send-keys","-l","-t","{sn}",{repr(pre)}])'
    sp.Popen([sys.executable, '-c', script], stdout=sp.DEVNULL, stderr=sp.DEVNULL)

# Help
HELP_SHORT = """a c|co|g|ai     Start claude/codex/gemini/aider
a <#>           Open project by number
a prompt        Manage default prompt
a help          All commands"""

def list_all(cache=True, quiet=False):
    H=os.path.expanduser('~'); mk=lambda p,r:'+' if os.path.exists(p) else('~'if r else'x')
    p,a = load_proj(), load_apps(); Path(os.path.join(DATA_DIR,'projects.txt')).write_text('\n'.join(x for x,_ in p)+'\n')
    out = ([f"PROJECTS:"]+[f"  {i}. {mk(x,r)} {x}" for i,(x,r) in enumerate(p)] if p else [])+([f"COMMANDS:"]+[f"  {len(p)+i}. {n} -> {c.replace(H,'~')[:57]}" for i,(n,c) in enumerate(a)] if a else [])
    txt='\n'.join(out); not quiet and out and print(txt); cache and Path(os.path.join(DATA_DIR,'help_cache.txt')).write_text(HELP_SHORT+'\n'+txt+'\n')
    return p, a

def parse_specs(argv, si, cfg):
    specs, parts, parsing = [], [], True
    for a in argv[si:]:
        if a in ['--seq', '--sequential']: continue
        if parsing and ':' in a and len(a) <= 4:
            p = a.split(':')
            if len(p) == 2 and p[0] in ['c', 'l', 'g'] and p[1].isdigit(): specs.append((p[0], int(p[1]))); continue
        parsing = False; parts.append(a)
    return (specs, cfg.get('codex_prompt', ''), True) if not parts else (specs, ' '.join(parts), False)
