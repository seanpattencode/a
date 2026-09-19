import sys, os, subprocess as S, time, platform, shlex
from os.path import exists, isdir, join, dirname, expanduser

PORT = 1111
_A = expanduser('~/.local/bin/a')
_MAC = platform.system() == 'Darwin'
_TERMUX = isdir('/data/data/com.termux')
_WSL = 'microsoft' in platform.release().lower()
_r = lambda c: S.run(c, capture_output=True)
_kill = lambda: _r(['pkill','-f','a serve'])

def _url(p): return f'http://localhost:{p}'

def _open(url):  # default browser per platform; WSL has no X -> hand the url to the Windows browser (mirrored localhost)
    if _WSL: S.Popen(['powershell.exe','-NoProfile','-c','Start-Process',url], stdout=S.DEVNULL, stderr=S.DEVNULL)
    elif _MAC: S.Popen(['open',url], stdout=S.DEVNULL, stderr=S.DEVNULL)
    elif _TERMUX: S.Popen(['termux-open',url], stdout=S.DEVNULL, stderr=S.DEVNULL)
    else: import webbrowser; webbrowser.open(url)

def _ask_open(p):  # offer the browser only when interactive; never block install/systemd (no tty)
    if not sys.stdin.isatty(): return
    try: r = input(f'open {_url(p)} in the default browser? [Y/n] ')
    except EOFError: return
    if r.strip().lower() in ('', 'y', 'yes'): _open(_url(p))

def _bg(p):
    pr = S.Popen([_A,'serve',str(p)], start_new_session=True, stdout=S.DEVNULL, stderr=S.DEVNULL); time.sleep(0.3)
    if pr.poll() is not None: print(f'x :{p} in use — another a serve has it (win+wsl share localhost)')
    else: _open(_url(p))

def _plist(): return expanduser('~/Library/LaunchAgents/com.a.ui.plist')
def _unit(): return expanduser('~/.config/systemd/user/a-ui.service')
def _svdir(): return join(os.environ.get('PREFIX', '/usr'), 'var/service/a-ui')

def _svc_off():
    if _MAC:
        p = _plist()
        if exists(p): _r(['launchctl', 'unload', p]); os.remove(p)
    elif _TERMUX:
        sd = _svdir()
        if isdir(sd): _r(['sv', 'down', 'a-ui']); _r(['rm', '-rf', sd])
    else:
        _r(['systemctl', '--user', 'disable', '--now', 'a-ui'])
        u = _unit()
        if exists(u): os.remove(u)

def _svc_on(p=PORT):
    rc = [expanduser('~/.local/bin/a'), 'serve', str(p)]; cmd = shlex.join(rc)
    if _MAC:
        pf = _plist(); _svc_off()
        os.makedirs(dirname(pf), exist_ok=True)
        args = ''.join(f'\n        <string>{a}</string>' for a in rc)
        with open(pf, 'w') as f:
            f.write(f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>com.a.ui</string>
    <key>ProgramArguments</key><array>{args}
    </array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>StandardErrorPath</key><string>/tmp/a-ui.err</string>
</dict>
</plist>''')
        _r(['launchctl', 'load', pf]); return True
    if _TERMUX:
        sd = _svdir(); _svc_off(); os.makedirs(sd, exist_ok=True); rs = join(sd, 'run')
        prefix = os.environ.get('PREFIX', '/data/data/com.termux/files/usr')
        with open(rs, 'w') as f: f.write(f'#!{prefix}/bin/sh\nexec {cmd}\n')
        os.chmod(rs, 0o755); _r(['sv', 'up', 'a-ui']).returncode and S.Popen(['sh', rs], start_new_session=True, stdout=S.DEVNULL, stderr=S.DEVNULL); return True
    if _r(['systemctl', '--user', '--version']).returncode == 0:
        _svc_off(); ud = dirname(_unit()); os.makedirs(ud, exist_ok=True)
        with open(_unit(), 'w') as f:
            f.write(f'[Unit]\nDescription=a UI server\n[Service]\nExecStart={cmd}\nRestart=always\nRestartSec=2\n[Install]\nWantedBy=default.target\n')
        _r(['systemctl', '--user', 'daemon-reload']); _r(['systemctl', '--user', 'enable', '--now', 'a-ui']); return True
    return False

def run():
    a = sys.argv[2:]
    if a and a[0][0] == 'k':
        _kill(); print('Killed (service will restart)')
    elif a and a[0] == 'on':
        if not _svc_on(): print('No service manager (use a ui)'); sys.exit(1)
        ok = _MAC or _TERMUX or (time.sleep(1) or _r(['systemctl', '--user', 'is-active', 'a-ui']).returncode == 0)
        print(f'UI service on — {_url(PORT)}' if ok else f'x a-ui failed — :{PORT} in use by another a? journalctl --user -u a-ui')
        if ok: _ask_open(PORT)
    elif a and a[0] == 'off':
        _svc_off(); _kill(); print('UI service off')
    elif a and a[0] == 'reload':  # restart :PORT so it serves the rebuilt binary — incl. unmanaged serves (dead-runsvdir termux) and stale per-connection children
        pat = f'a serve {PORT}$'
        had = _r(['pgrep', '-f', pat]).returncode == 0
        _r(['pkill', '-f', pat])
        if _MAC: _r(['launchctl', 'kickstart', '-k', f'gui/{os.getuid()}/com.a.ui'])
        elif _TERMUX: _r(['sv', 'restart', 'a-ui'])
        elif _r(['systemctl', '--user', 'is-active', 'a-ui']).returncode == 0: _r(['systemctl', '--user', 'restart', 'a-ui'])
        if had:
            time.sleep(.4)  # let the manager's fresh child appear before deciding it isn't coming
            if _r(['pgrep', '-f', pat]).returncode: S.Popen([_A, 'serve', str(PORT)], start_new_session=True, stdout=S.DEVNULL, stderr=S.DEVNULL)  # ran unmanaged -> respawn (no browser)
    else:
        p = int(a[0]) if a and a[0].isdigit() else PORT
        _kill(); _bg(p)
        print(f"{_url(p)}\n  on  auto-start service\n  off stop service\n  k   kill")

if __name__ == '__main__': run()
