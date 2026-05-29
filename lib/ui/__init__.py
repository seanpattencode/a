import sys, os, subprocess as S, time, platform, shlex
from os.path import exists, isdir, join, dirname, expanduser

PORT = 1111
_A = expanduser('~/.local/bin/a')
_MAC = platform.system() == 'Darwin'
_TERMUX = isdir('/data/data/com.termux')
_r = lambda c: S.run(c, capture_output=True)
_kill = lambda: _r(['pkill','-f','a serve'])

def _url(p): return f'http://localhost:{p}'

def _bg(p):
    S.Popen([_A,'serve',str(p)], start_new_session=True, stdout=S.DEVNULL, stderr=S.DEVNULL)
    time.sleep(0.3); import webbrowser; webbrowser.open(_url(p))

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
        if _svc_on(): print(f'UI service on — {_url(PORT)}')
        else: print('No service manager (use a ui)'); sys.exit(1)
    elif a and a[0] == 'off':
        _svc_off(); _kill(); print('UI service off')
    elif a and a[0] == 'reload':  # restart the managed service so it picks up a rebuilt binary; silent no-op if unmanaged
        if _MAC: _r(['launchctl', 'kickstart', '-k', f'gui/{os.getuid()}/com.a.ui'])
        elif _TERMUX: _r(['sv', 'restart', 'a-ui'])
        elif _r(['systemctl', '--user', 'is-active', 'a-ui']).returncode == 0: _r(['systemctl', '--user', 'restart', 'a-ui'])
    else:
        p = int(a[0]) if a and a[0].isdigit() else PORT
        _kill(); _bg('ui_full', p)
        print(f"{_url(p)}\n  on  auto-start service\n  off stop service\n  k   kill")

if __name__ == '__main__': run()
