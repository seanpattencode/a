#!/usr/bin/env python3
# experimental
"""cdp.py — minimal browser control via CDP. <2s to page loaded.
Usage: python3 cdp.py <url> [me] [canary|dev|beta]
"""
import sys, os, time, json, shutil, subprocess
import urllib.request
import websocket

PORT = 9222
TIMEOUT = 2.0  # perf kill

def _die(msg): print(f'✗ {msg}'); sys.exit(1)
def _http(path): return json.loads(urllib.request.urlopen(f'http://localhost:{PORT}{path}', timeout=1).read())
def _cdp_up():
    try: _http('/json'); return True
    except: return False

def _launch(variant='stable', me=False, url=None):
    """Launch Chrome with CDP against a non-default profile path (Chrome 136+ disables
    --remote-debugging-port on default profile dirs). With me=True, copy cookies and
    Login Data from real profile so sessions persist across scraper restarts."""
    bins = {'stable': 'google-chrome', 'beta': 'google-chrome-beta',
            'canary': 'google-chrome-canary', 'dev': 'google-chrome-unstable'}
    binary = None
    for v in [variant] + [k for k in bins if k != variant]:
        if shutil.which(bins[v]): binary = bins[v]; break
    if not binary: _die('no chrome found')

    profile = os.path.expanduser(f'~/.cache/cdp-{variant}')
    if me:
        src_map = {'stable':'~/.config/google-chrome','beta':'~/.config/google-chrome-beta',
                   'canary':'~/.config/google-chrome-canary','dev':'~/.config/google-chrome-unstable'}
        src = next((os.path.expanduser(src_map[v]) for v in [variant]+list(src_map)
                    if os.path.isdir(os.path.expanduser(src_map[v]))), None)
        if src:
            os.makedirs(f'{profile}/Default', exist_ok=True)
            for f in ['Cookies','Cookies-journal','Login Data','Preferences']:
                s=f'{src}/Default/{f}'
                if os.path.exists(s):
                    try: shutil.copy2(s, f'{profile}/Default/{f}')
                    except: pass
            sl=f'{src}/Local State'
            if os.path.exists(sl):
                try: shutil.copy2(sl, f'{profile}/Local State')
                except: pass

    args = [binary, f'--user-data-dir={profile}', f'--remote-debugging-port={PORT}',
            '--remote-allow-origins=*', '--no-first-run', '--disable-gpu-sandbox',
            '--disable-session-crashed-bubble', '--disable-features=SessionRestore',
            '--password-store=basic',  # avoids 25s D-Bus timeout on Secret Service cookie-jar key
            '--disable-background-networking']

    import glob
    socks = [s for s in glob.glob(f'/run/user/{os.getuid()}/sway-ipc.*.sock')
             if subprocess.run(['swaymsg','-s',s,'-t','get_version'],capture_output=True,timeout=1).returncode==0]
    if socks:
        args += ['--ozone-platform=wayland','--enable-features=UseOzonePlatform']
        subprocess.run(['swaymsg','-s',socks[0],'exec',
                        ' '.join(f"'{a}'" if ' ' in a or '=' in a else a for a in args)],
                       capture_output=True)
    else:
        subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True)

    # Fire and forget. Chrome cold-start is ~5s on its own; don't block the
    # caller here. Caller polls _cdp_up() or returns with a "retry" message.

def connect(url=None):
    """Create fresh tab, navigate if url given. Returns (ws, target_id)."""
    info = _http('/json/version')
    bws = websocket.create_connection(info['webSocketDebuggerUrl'], timeout=5)
    bws.send(json.dumps({'id': 1, 'method': 'Target.createTarget', 'params': {'url': 'about:blank'}}))
    resp = json.loads(bws.recv())
    tid = resp.get('result', {}).get('targetId')
    bws.close()
    if not tid: _die('createTarget failed')
    targets = _http('/json')
    target = next((t for t in targets if t['id'] == tid), None)
    if not target: _die(f'target {tid} gone')
    ws = websocket.create_connection(target['webSocketDebuggerUrl'], timeout=5)
    if url:
        ws.send(json.dumps({'id': 1, 'method': 'Page.enable'}))
        ws.send(json.dumps({'id': 2, 'method': 'Page.navigate', 'params': {'url': url}}))
    return ws, target['id']

def wait_loaded(ws, timeout=10):
    """Event-driven: block inside Chrome until document.readyState==='complete'.
    Runtime.evaluate with awaitPromise waits in-browser; no polling."""
    expr = 'new Promise(r=>{if(document.readyState==="complete")r();else addEventListener("load",r,{once:true})})'
    ws.settimeout(timeout)
    ws.send(json.dumps({'id': 90, 'method': 'Runtime.evaluate',
                        'params': {'expression': expr, 'awaitPromise': True, 'returnByValue': True}}))
    while True:
        try:
            r = json.loads(ws.recv())
            if r.get('id') == 90: return True
        except: return False

def js(ws, expr):
    """Evaluate JS, return value."""
    ws.settimeout(5)
    ws.send(json.dumps({'id': 99, 'method': 'Runtime.evaluate',
                        'params': {'expression': expr, 'returnByValue': True}}))
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            r = json.loads(ws.recv())
            if r.get('id') == 99:
                return r.get('result', {}).get('result', {}).get('value')
        except: return None
    return None

def click(ws, sel):
    """Click element by CSS selector."""
    js(ws, f'document.querySelector({json.dumps(sel)})?.click()')

def fill(ws, sel, text):
    """Click + type into element."""
    rect = js(ws, f'JSON.stringify(document.querySelector({json.dumps(sel)})?.getBoundingClientRect())')
    if rect:
        pt = json.loads(rect)
        for t in ('mousePressed', 'mouseReleased'):
            ws.send(json.dumps({'id': 0, 'method': 'Input.dispatchMouseEvent',
                                'type': t, 'x': pt['x']+pt['width']/2, 'y': pt['y']+pt['height']/2,
                                'button': 'left', 'clickCount': 1}))
        time.sleep(0.1)
    ws.send(json.dumps({'id': 0, 'method': 'Input.insertText', 'params': {'text': text}}))

def key(ws, k):
    """Press key."""
    codes = {'Enter': 13, 'Tab': 9, 'Escape': 27}
    vk = codes.get(k, 0)
    base = {'key': k, 'code': k, 'windowsVirtualKeyCode': vk}
    if k == 'Enter': base['text'] = '\r'
    ws.send(json.dumps({'id': 0, 'method': 'Input.dispatchKeyEvent', **base, 'type': 'keyDown'}))
    ws.send(json.dumps({'id': 0, 'method': 'Input.dispatchKeyEvent', **base, 'type': 'keyUp'}))

def screenshot(ws, path='/tmp/cdp.png'):
    """Capture screenshot."""
    import base64
    ws.settimeout(5)
    try:
        ws.send(json.dumps({'id': 98, 'method': 'Page.captureScreenshot', 'params': {'format': 'png'}}))
        deadline = time.time() + 5
        while time.time() < deadline:
            r = json.loads(ws.recv())
            if r.get('id') == 98:
                with open(path, 'wb') as f: f.write(base64.b64decode(r['result']['data']))
                return path
    except: pass
    return None

if __name__ == '__main__':
    args = set(sys.argv[1:])
    url = next((a for a in args if a.startswith('http') or '.' in a), None)
    if url and not url.startswith('http'): url = f'https://{url}'
    me = 'me' in args; args.discard('me')
    variant = next((a for a in args if a in ('canary', 'dev', 'beta', 'stable')), 'beta')
    args.discard(variant)
    if url: args.discard(url.replace('https://', '').replace('http://', ''))

    t0 = time.time()

    if not _cdp_up():
        _launch(variant, me, url)

    ws, tid = connect(url)
    t1 = time.time()

    if url:
        wait_loaded(ws)

    t2 = time.time()
    final_url = js(ws, 'location.href')
    print(f'{(t1-t0)*1000:.0f}ms connect  {(t2-t0)*1000:.0f}ms loaded  {final_url}')

    if t2 - t0 > TIMEOUT:
        print(f'✗ PERF: {(t2-t0)*1000:.0f}ms > {TIMEOUT*1000:.0f}ms limit')

    screenshot(ws)
    ws.close()
