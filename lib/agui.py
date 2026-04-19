#!/usr/bin/env python3
# experimental
"""agui — browser automation via raw CDP. Playwright optional (--pw).
Connects to user's default browser on CDP port 9222, launches if needed.
"""

import subprocess, sys, os

import time
import random
import asyncio
import atexit
import shutil
import platform

# Optional dependencies - graceful fallback
try:
    import requests
except ImportError:
    requests = None

try:
    import pdfplumber
except ImportError:
    pdfplumber = None

# Playwright optional — only needed with --pw flag
try:
    from playwright.sync_api import sync_playwright
    from playwright.async_api import async_playwright
    _HAS_PLAYWRIGHT = True
except ImportError:
    _HAS_PLAYWRIGHT = False

# Platform detection
IS_MAC = platform.system() == 'Darwin'
IS_LINUX = platform.system() == 'Linux'

# Sway detection — auto-find socket, set env for swaymsg + wayland clients
IS_SWAY = False
_SWAYSOCK = os.environ.get('SWAYSOCK', '')
if not _SWAYSOCK and IS_LINUX:
    import glob as _g
    for _s in reversed(sorted(_g.glob(f'/run/user/{os.getuid()}/sway-ipc.*.sock'))):
        if subprocess.run(['swaymsg', '-s', _s, '-t', 'get_version'],
                          capture_output=True, timeout=2).returncode == 0:
            _SWAYSOCK = _s; break
if _SWAYSOCK:
    IS_SWAY = True
    os.environ['SWAYSOCK'] = _SWAYSOCK
    # Detect Sway's actual WAYLAND_DISPLAY by asking swaymsg
    _r = subprocess.run(['swaymsg', '-s', _SWAYSOCK, 'exec', 'echo $WAYLAND_DISPLAY > /tmp/.sway_wl'],
                        capture_output=True, timeout=3)
    import time as _t; _t.sleep(0.3)
    try:
        _sway_wl = open('/tmp/.sway_wl').read().strip()
    except: _sway_wl = ''
    if _sway_wl:
        os.environ['WAYLAND_DISPLAY'] = _sway_wl
    elif not os.environ.get('WAYLAND_DISPLAY'):
        os.environ['WAYLAND_DISPLAY'] = 'wayland-0'

def _swaymsg(*args):
    """Run swaymsg with correct socket"""
    return subprocess.run(['swaymsg'] + list(args), capture_output=True, text=True,
                          env={**os.environ, 'SWAYSOCK': _SWAYSOCK})

# Enable unbuffered output (library glue)
sys.stdout = os.fdopen(sys.stdout.fileno(), 'w', buffering=1)
sys.stderr = os.fdopen(sys.stderr.fileno(), 'w', buffering=1)

# Global state
_playwright = None
_browser = None
_browser_context = None
_page = None
_chrome_process = None
_profile_dir = None
_center_mode = True  # Default: open on primary/center monitor
_headless_mode = False
_copy_profile = False  # Default: separate session (isolated profile)
_chrome_variant = 'beta'  # legacy; no longer drives binary/profile choice (see _automation_choice)
_use_cdp = True  # True = pure CDP (default), False = playwright over CDP (--pw)

# ═══════════════════════════════════════════════════════════
# CDP DIRECT — Pure Chrome DevTools Protocol, no Playwright
# ═══════════════════════════════════════════════════════════

class CDPPage:
    """Minimal CDP page controller — stdlib + websocket-client only"""
    def __init__(self, ws_url):
        import websocket as _ws
        self._ws = _ws.create_connection(ws_url, timeout=30)
        self._id = 0
        self.url = 'about:blank'
        self._vp = None

    def _send(self, method, params=None):
        import json
        self._id += 1
        msg = {'id': self._id, 'method': method}
        if params: msg['params'] = params
        self._ws.send(json.dumps(msg))
        while True:
            r = json.loads(self._ws.recv())
            if r.get('id') == self._id: return r.get('result', {})
            # Stash events for watch_text
            if r.get('method') == 'Runtime.bindingCalled':
                self._events.append(r.get('params', {}).get('payload', ''))

    _events = []

    def goto(self, url, wait_until='domcontentloaded', timeout=30000):
        import json, time
        self._send('Page.enable')
        # Fire navigate without waiting for response (Chrome blocks during session restore)
        self._id += 1
        nav_id = self._id
        self._ws.send(json.dumps({'id': nav_id, 'method': 'Page.navigate', 'params': {'url': url}}))
        # Wait for load event OR navigate response
        deadline = time.time() + timeout / 1000
        while time.time() < deadline:
            try:
                self._ws.settimeout(2)
                r = json.loads(self._ws.recv())
                m = r.get('method', '')
                if m in ('Page.loadEventFired', 'Page.domContentEventFired'): break
                if m == 'Runtime.bindingCalled':
                    self._events.append(r.get('params', {}).get('payload', ''))
            except: pass
        self._ws.settimeout(30)
        self.url = self._eval('location.href') or url

    def screenshot(self, path=None, **kw):
        import base64
        r = self._send('Page.captureScreenshot', {'format': 'png'})
        data = base64.b64decode(r.get('data', ''))
        if path:
            with open(path, 'wb') as f: f.write(data)
        return data

    def _eval(self, expr):
        r = self._send('Runtime.evaluate', {'expression': expr, 'returnByValue': True})
        v = r.get('result', {}).get('value')
        return v

    def evaluate(self, expr): return self._eval(expr)

    def query_selector(self, sel):
        return self._eval(f'!!document.querySelector({repr(sel)})')

    def click(self, sel, timeout=5000):
        self._eval(f'document.querySelector({repr(sel)})?.click()')

    def wait_for_selector(self, sel, timeout=5000, state='visible'):
        import time
        deadline = time.time() + timeout / 1000
        while time.time() < deadline:
            if self._eval(f'!!document.querySelector({repr(sel)})'): return True
            time.sleep(0.2)
        return False

    def fill(self, sel, text):
        # Click element center to focus, then insertText
        r = self._eval(f'''(()=>{{let e=document.querySelector({repr(sel)});if(!e)return null;let b=e.getBoundingClientRect();return JSON.stringify({{x:b.x+b.width/2,y:b.y+b.height/2}})}})()''')
        if r:
            import json as _j
            pt = _j.loads(r)
            self._send('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': pt['x'], 'y': pt['y'], 'button': 'left', 'clickCount': 1})
            self._send('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': pt['x'], 'y': pt['y'], 'button': 'left', 'clickCount': 1})
            import time; time.sleep(0.3)
        # Select all existing text then insert
        self._send('Input.dispatchKeyEvent', {'type': 'keyDown', 'key': 'a', 'code': 'KeyA', 'windowsVirtualKeyCode': 65, 'nativeVirtualKeyCode': 65, 'modifiers': 2})
        self._send('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': 'a', 'code': 'KeyA', 'modifiers': 2})
        self._send('Input.insertText', {'text': text})

    def type(self, sel, text, delay=0):
        self.fill(sel, text)

    def get_by_text(self, text, exact=False):
        return self  # chainable stub

    @property
    def first(self):
        return self

    def count(self):
        return 0  # stub for check_text_exists

    def locator(self, sel):
        return _CDPLocator(self, sel)

    @property
    def keyboard(self):
        return _CDPKeyboard(self)

    @property
    def viewport_size(self):
        return self._vp or {'width': 1920, 'height': 1080}

    def set_viewport_size(self, vp):
        self._vp = vp
        self._send('Emulation.setDeviceMetricsOverride',
                    {'width': vp['width'], 'height': vp['height'], 'deviceScaleFactor': 1, 'mobile': False})

    def bring_to_front(self):
        self._send('Page.bringToFront')

    def watch_text(self, sel=None, timeout=60):
        """Event-driven text streaming via MutationObserver + CDP binding.
        sel: CSS selector to watch (default: document.body). Works on any OS.
        Yields (new_text, full_text) tuples as content changes."""
        import json, time
        self._send('Runtime.enable')
        self._send('Runtime.addBinding', {'name': '_cdpText'})
        target = f'document.querySelector({json.dumps(sel)})' if sel else 'document.body'
        self._eval(f'''(()=>{{
            let el={target};if(!el)el=document.body;
            let prev=el.innerText;
            new MutationObserver(()=>{{
                let cur=el.innerText;
                if(cur!==prev){{_cdpText(cur);prev=cur}}
            }}).observe(el,{{childList:true,subtree:true,characterData:true}});
        }})()''')
        self._events = []
        deadline = time.time() + timeout
        prev_text = ''
        last_change = time.time()
        while time.time() < deadline:
            self._ws.settimeout(0.5)
            try:
                r = json.loads(self._ws.recv())
                if r.get('method') == 'Runtime.bindingCalled':
                    text = r.get('params', {}).get('payload', '')
                    if text != prev_text:
                        new = text[len(prev_text):] if len(text) > len(prev_text) else text
                        yield new, text
                        prev_text = text
                        last_change = time.time()
            except: pass
            if prev_text and time.time() - last_change > 3:
                break
        self._ws.settimeout(30)

    def close(self):
        try: self._ws.close()
        except: pass

class _CDPLocator:
    def __init__(self, page, sel): self._p, self._sel = page, sel
    @property
    def first(self): return self
    def click(self, timeout=5000): self._p.click(self._sel, timeout)
    def fill(self, text, timeout=5000): self._p.fill(self._sel, text)

class _CDPKeyboard:
    _CODES = {'Enter': 13, 'Tab': 9, 'Escape': 27, 'Backspace': 8, 'Delete': 46,
              'ArrowUp': 38, 'ArrowDown': 40, 'ArrowLeft': 37, 'ArrowRight': 39}
    def __init__(self, page): self._p = page
    def press(self, key):
        vk = self._CODES.get(key, 0)
        base = {'type': 'keyDown', 'key': key, 'code': key, 'windowsVirtualKeyCode': vk}
        if key == 'Enter': base['text'] = '\r'
        elif key == 'Tab': base['text'] = '\t'
        self._p._send('Input.dispatchKeyEvent', base)
        self._p._send('Input.dispatchKeyEvent', {'type': 'keyUp', 'key': key, 'code': key, 'windowsVirtualKeyCode': vk})
    def type(self, text, delay=0):
        self._p._send('Input.insertText', {'text': text})

# ═══════════════════════════════════════════════════════════
# STDIN/PASTE INPUT - Read pasted content (library glue)
# ═══════════════════════════════════════════════════════════

def read_pasted_content():
    """Read pasted content from stdin (library glue)"""
    if not sys.stdin.isatty():
        return sys.stdin.read().strip()
    print("\n" + "═" * 50)
    print("PASTE YOUR CONTENT (PDF text, document, etc.)")
    print("═" * 50)
    print("→ Paste below, then press Ctrl+D (Linux/Mac) or Ctrl+Z+Enter (Windows)")
    print("─" * 50 + "\n")
    return sys.stdin.read().strip()

def extract_pdf_text(pdf_path):
    """Extract text from PDF file using pdfplumber (library glue)"""
    if pdfplumber is None:
        print("  Warning: pdfplumber not installed. Install with: pip install pdfplumber")
        return ""
    with pdfplumber.open(pdf_path) as pdf:
        return '\n\n'.join(page.extract_text() or '' for page in pdf.pages)

# ═══════════════════════════════════════════════════════════
# SYSTEM TOOLS - XWayland library glue (xdotool, xrandr)
# ═══════════════════════════════════════════════════════════

def run_cmd(cmd):
    """Execute command (library glue)"""
    return subprocess.run(cmd, capture_output=True, text=True)

def get_monitors():
    """Get monitor geometry (library glue) - Linux only"""
    if IS_MAC:
        return [{'x': 0, 'y': 0, 'width': 1920, 'height': 1080}]
    if IS_SWAY:
        import json as _j
        r = _swaymsg('-t', 'get_outputs')
        monitors = []
        for o in _j.loads(r.stdout) if r.returncode == 0 else []:
            if o.get('active'):
                rect = o['rect']
                monitors.append({'x': rect['x'], 'y': rect['y'], 'width': rect['width'],
                                 'height': rect['height'], 'name': o['name'], 'focused': o.get('focused', False)})
        return monitors
    result = run_cmd(['xrandr', '--query'])
    monitors = []
    for line in result.stdout.split('\n'):
        if ' connected' in line and 'x' in line:
            for part in line.split():
                if 'x' in part and '+' in part:
                    geometry, pos = part.split('+', 1)
                    w, h = map(int, geometry.split('x'))
                    x, y = map(int, pos.split('+'))
                    monitors.append({'x': x, 'y': y, 'width': w, 'height': h})
                    break
    return monitors

def get_rightmost_monitor():
    """Get rightmost monitor (library glue)"""
    monitors = get_monitors()
    return max(monitors, key=lambda m: m['x']) if monitors else None

def get_primary_monitor():
    """Get primary/focused monitor (library glue)"""
    if IS_MAC:
        return {'x': 0, 'y': 0, 'width': 1920, 'height': 1080, 'name': 'default'}
    if IS_SWAY:
        monitors = get_monitors()
        focused = next((m for m in monitors if m.get('focused')), None)
        return focused or (monitors[0] if monitors else None)
    result = run_cmd(['xrandr', '--query'])
    for line in result.stdout.split('\n'):
        if ' connected' in line and ' primary ' in line:
            for part in line.split():
                if 'x' in part and '+' in part:
                    geometry, pos = part.split('+', 1)
                    w, h = map(int, geometry.split('x'))
                    x, y = map(int, pos.split('+'))
                    return {'x': x, 'y': y, 'width': w, 'height': h, 'name': line.split()[0]}
    monitors = get_monitors()
    if monitors:
        # Prefer monitor at origin, else leftmost
        origin = next((m for m in monitors if m['x'] == 0 and m['y'] == 0), None)
        return origin or min(monitors, key=lambda m: m['x'])
    return None

def find_chrome_window():
    """Find Chrome window (library glue) - Linux only"""
    if IS_MAC:
        return None
    if IS_SWAY:
        import json as _j
        r = _swaymsg('-t', 'get_tree')
        if r.returncode != 0: return None
        # Walk sway tree to find chrome/chromium
        def _find(node):
            aid = (node.get('app_id') or '').lower()
            name = (node.get('name') or '').lower()
            if 'chrom' in aid or 'chrom' in name or 'google-chrome' in aid:
                return node.get('id')
            for c in node.get('nodes', []) + node.get('floating_nodes', []):
                r = _find(c)
                if r: return r
        return _find(_j.loads(r.stdout))
    result = run_cmd(['xdotool', 'search', '--class', 'chrome'])
    windows = result.stdout.strip().split('\n') if result.stdout.strip() else []
    return windows[-1] if windows else None

def move_window(window_id, x, y):
    """Move window (library glue) - Linux only"""
    if IS_MAC or not window_id: return
    if IS_SWAY:
        _swaymsg(f'[con_id={window_id}]', 'move', 'position', str(x), str(y))
        return
    run_cmd(['xdotool', 'windowmove', str(window_id), str(x), str(y)])

def resize_window(window_id, width, height):
    """Resize window (library glue) - Linux only"""
    if IS_MAC or not window_id: return
    if IS_SWAY:
        _swaymsg(f'[con_id={window_id}]', 'resize', 'set', str(width), str(height))
        return
    run_cmd(['xdotool', 'windowsize', str(window_id), str(width), str(height)])

def check_tool_available(tool):
    """Check if tool is available (library glue)"""
    return run_cmd(['which', tool]).returncode == 0

_automation_cache = None
def _automation_choice():
    """Pick (profile_variant, launch_binary). Profile = idle Chrome variant; binary = a different variant
    so Chrome 136+ doesn't silently disable --remote-debugging-port on its own default profile dir."""
    global _automation_cache
    if _automation_cache: return _automation_cache
    for v in ('beta','unstable'):
        d = os.path.expanduser(f'~/.config/google-chrome-{v}')
        if not os.path.isdir(d): continue
        if subprocess.run(['pgrep','-f',f'/opt/google/chrome-{v}/chrome --type='],capture_output=True).returncode == 0: continue
        for lv in ('beta','unstable','canary',''):
            if lv == v: continue
            b = shutil.which(f'google-chrome-{lv}' if lv else 'google-chrome')
            if b: _automation_cache = (v, b); return _automation_cache
    raise RuntimeError("No free Chrome profile. Close google-chrome-beta or -unstable, or install both.")

def _resolve_chrome_binary():
    """Pick Chrome binary — Mac uses any installed; Linux uses the cross-binary chosen by _automation_choice."""
    if IS_MAC:
        for p in ('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
                   '/Applications/Google Chrome Beta.app/Contents/MacOS/Google Chrome Beta',
                   '/Applications/Google Chrome Dev.app/Contents/MacOS/Google Chrome Dev',
                   '/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary',
                   '/Applications/Chromium.app/Contents/MacOS/Chromium'):
            if os.path.exists(p): return p
        if shutil.which('chromium'): return shutil.which('chromium')
        raise RuntimeError("Chrome/Chromium not found on Mac.")
    return _automation_choice()[1]

def _terminate_profile_processes(user_data_dir, wait_timeout=5):
    """
    Terminate any Chrome instances still attached to the automation profile.
    This avoids stale remote-debugging ports on re-run.
    """
    pattern = f'--user-data-dir={user_data_dir}'
    for signal_flag in ([], ['-9']):
        cmd = ['pkill', *signal_flag, '-f', '--', pattern]
        subprocess.run(cmd, capture_output=True, text=True)

    if not check_tool_available('pgrep'):
        return

    start = time.time()
    while time.time() - start < wait_timeout:
        probe = subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True)
        if probe.returncode != 0:
            break
        time.sleep(0.2)

    for lock_name in ('SingletonLock', 'SingletonCookie', 'SingletonSocket'):
        lock_path = os.path.join(user_data_dir, lock_name)
        try:
            if os.path.exists(lock_path) or os.path.islink(lock_path):
                os.unlink(lock_path)
        except OSError:
            pass

def _get_target_monitor():
    """Get target monitor based on mode (center_mode or right)"""
    if _headless_mode:
        return None
    if _center_mode:
        monitor = get_primary_monitor()
        if monitor:
            return monitor
        # Fallback to rightmost if no primary
        return get_rightmost_monitor()
    else:
        return get_rightmost_monitor()

def _prepare_chrome_launch(user_data_dir):
    """Build Chrome launch command and environment"""
    env = os.environ.copy()

    # Common Chrome args for all platforms
    chrome_args = [
        _resolve_chrome_binary(),
        f'--user-data-dir={user_data_dir}',
        '--remote-debugging-port=9222',
        '--remote-debugging-address=127.0.0.1',
        '--remote-allow-origins=*',
        '--no-first-run',
        '--no-default-browser-check',
        '--disable-blink-features=AutomationControlled',
        '--disable-session-crashed-bubble',
        '--start-maximized',
        'about:blank'
    ]

    if IS_MAC:
        chrome_args.insert(-1, '--disable-features=SessionRestore')
    elif IS_SWAY:
        # Native Wayland on Sway — no XWayland needed
        env['WAYLAND_DISPLAY'] = os.environ.get('WAYLAND_DISPLAY', 'wayland-0')
        env['XDG_SESSION_TYPE'] = 'wayland'
        env.pop('DISPLAY', None)
        env.pop('GDK_BACKEND', None)
        chrome_args.insert(-1, '--ozone-platform=wayland')
        chrome_args.insert(-1, '--disable-gpu-sandbox')
        chrome_args.insert(-1, '--disable-features=SessionRestore')
        chrome_args.insert(-1, '--enable-features=UseOzonePlatform')
    else:
        # GNOME/X11 fallback
        if 'DISPLAY' not in env or not env['DISPLAY']:
            env['DISPLAY'] = ':0'
        env.update({
            'GDK_BACKEND': 'x11',
            'QT_QPA_PLATFORM': 'xcb',
            'CLUTTER_BACKEND': 'x11',
            'SDL_VIDEODRIVER': 'x11'
        })
        env.pop('WAYLAND_DISPLAY', None)
        chrome_args.insert(-1, '--ozone-platform=x11')
        chrome_args.insert(-1, '--gtk-version=3')
        chrome_args.insert(-1, '--disable-gpu-sandbox')
        chrome_args.insert(-1, '--disable-features=UseOzonePlatform,WaylandWindowDecorations,SessionRestore')

    return chrome_args, env

def _position_chrome_window(target_monitor=None, tolerance=40):
    """Move Chrome window to target monitor"""
    if _headless_mode:
        print("  → Headless mode: skipping window positioning")
        return None

    if IS_MAC:
        print("  → Mac: skipping window positioning")
        time.sleep(2)
        return None

    if IS_SWAY:
        # Poll for chrome window via swaymsg tree
        window_id = None
        for _ in range(30):
            window_id = find_chrome_window()
            if window_id: break
            time.sleep(0.5)
        if not window_id:
            print("  → Chrome window not found in sway tree")
            return None
        monitor = target_monitor or _get_target_monitor()
        if monitor and monitor.get('name'):
            _swaymsg(f'[con_id={window_id}]', 'move', 'to', 'output', monitor['name'])
            print(f"  ✓ Moved to {monitor['name']}")
        return window_id

    # X11/GNOME path
    result = subprocess.run(
        ['xdotool', 'search', '--sync', '--onlyvisible', '--class', 'chrome'],
        capture_output=True, text=True, timeout=15
    )
    if not result.stdout.strip():
        print("  → Window not found")
        return None

    window_id = result.stdout.strip().split('\n')[0]
    monitor = target_monitor if target_monitor else _get_target_monitor()
    if not monitor:
        return window_id

    mode_name = "primary/center" if _center_mode else "rightmost"
    print(f"  → Moving to {mode_name} monitor...")

    has_wmctrl = check_tool_available('wmctrl')
    if has_wmctrl:
        subprocess.run(['wmctrl', '-i', '-r', window_id, '-b', 'remove,maximized_vert,maximized_horz'], capture_output=True)
    subprocess.run(['xdotool', 'windowactivate', '--sync', window_id], capture_output=True)
    subprocess.run(['xdotool', 'windowmove', '--sync', window_id, str(monitor['x']), str(monitor['y'])], capture_output=True)
    subprocess.run(['xdotool', 'windowsize', '--sync', window_id, str(monitor['width']), str(monitor['height'])], capture_output=True)
    if has_wmctrl:
        subprocess.run(['wmctrl', '-i', '-r', window_id, '-b', 'add,maximized_vert,maximized_horz'], capture_output=True)

    if not verify_window_on_monitor(window_id, monitor, tolerance=tolerance):
        if has_wmctrl:
            subprocess.run(['wmctrl', '-i', '-r', window_id, '-b', 'remove,maximized_vert,maximized_horz'], capture_output=True)
        subprocess.run(['xdotool', 'windowmove', '--sync', window_id, str(monitor['x']), str(monitor['y'])], capture_output=True)
        subprocess.run(['xdotool', 'windowsize', '--sync', window_id, str(monitor['width']), str(monitor['height'])], capture_output=True)
        if has_wmctrl:
            subprocess.run(['wmctrl', '-i', '-r', window_id, '-b', 'add,maximized_vert,maximized_horz'], capture_output=True)

    geom = get_window_position(window_id) or {}
    print(f"  ✓ Window at ({geom.get('x')},{geom.get('y')}) size {geom.get('width')}x{geom.get('height')}")
    return window_id

def maximize_window_on_monitor(window_id, monitor):
    """Position and maximize window on monitor (library glue)"""
    if IS_SWAY and window_id:
        if monitor and monitor.get('name'):
            _swaymsg(f'[con_id={window_id}]', 'move', 'to', 'output', monitor['name'])
        return
    move_window(window_id, monitor['x'], monitor['y'])
    time.sleep(0.15)

    if check_tool_available('wmctrl'):
        window_str = str(window_id)
        run_cmd(['wmctrl', '-i', '-r', window_str, '-b', 'remove,maximized_vert,maximized_horz'])
        run_cmd([
            'wmctrl', '-i', '-r', window_str, '-e',
            f"0,{monitor['x']},{monitor['y']},{monitor['width']},{monitor['height']}"
        ])
        run_cmd(['wmctrl', '-i', '-r', window_str, '-b', 'add,maximized_vert,maximized_horz'])
    else:
        resize_window(window_id, monitor['width'], monitor['height'])

def get_window_position(window_id):
    """Get window position and size (library glue) - Linux only"""
    if IS_MAC or not window_id:
        return None
    if IS_SWAY:
        import json as _j
        r = _swaymsg('-t', 'get_tree')
        if r.returncode != 0: return None
        def _find(node):
            if node.get('id') == window_id:
                rect = node.get('rect', {})
                return {'x': rect.get('x',0), 'y': rect.get('y',0),
                        'width': rect.get('width',0), 'height': rect.get('height',0)}
            for c in node.get('nodes', []) + node.get('floating_nodes', []):
                r = _find(c)
                if r: return r
        return _find(_j.loads(r.stdout))
    result = run_cmd(['xdotool', 'getwindowgeometry', str(window_id)])
    info = {}
    for line in result.stdout.split('\n'):
        if 'Position:' in line:
            pos_str = line.split('Position:')[1].split('(')[0].strip()
            x, y = map(int, pos_str.split(','))
            info['x'] = x
            info['y'] = y
        elif 'Geometry:' in line:
            geometry = line.split('Geometry:')[1].strip()
            if 'x' in geometry:
                width, height = map(int, geometry.split('x')[0:2])
                info['width'] = width
                info['height'] = height
    return info if 'x' in info and 'y' in info else None

def verify_window_on_monitor(window_id, monitor, tolerance=100):
    """Verify window is on target monitor (library glue)"""
    geom = get_window_position(window_id)
    if not geom:
        return False
    width = geom.get('width')
    height = geom.get('height')
    size_ok = True
    if width and height:
        size_ok = (
            abs(width - monitor['width']) <= tolerance and
            abs(height - monitor['height']) <= tolerance
        )
    x_ok = monitor['x'] - tolerance <= geom['x'] <= monitor['x'] + monitor['width'] + tolerance
    return x_ok and size_ok

# ═══════════════════════════════════════════════════════════
# BROWSER LIFECYCLE - Native Wayland with CDP
# ═══════════════════════════════════════════════════════════

def get_profile_path():
    """Real Chrome profile dir of the chosen automation variant. Persistent — login once, reuse forever."""
    if IS_MAC: return os.path.expanduser('~/Library/Application Support/agui/chrome-profile')
    return os.path.expanduser(f'~/.config/google-chrome-{_automation_choice()[0]}')

# ─── EXPERIMENTAL: Profile Grabbing ───────────────────────────
# These functions copy cookies/passwords from real Chrome profile.
# Commented out as experimental - enable _copy_profile flag to use.
#
# def _get_real_chrome_profile():
#     """Get real Chrome profile path based on _chrome_variant setting"""
#     paths = ['~/.config/google-chrome-beta', '~/.config/google-chrome'] if _chrome_variant == 'beta' else ['~/.config/google-chrome', '~/.config/google-chrome-beta']
#     for p in paths:
#         expanded = os.path.expanduser(p)
#         if os.path.isdir(expanded): return expanded
#
# def _sync_profile_data():
#     """Copy cookies/passwords/extensions from real Chrome (one-time or refresh)"""
#     src = _get_real_chrome_profile()
#     if not src:
#         print("  → No Chrome profile found to copy from")
#         return
#     dst = get_profile_path()
#     os.makedirs(dst, exist_ok=True)
#     items = ['Default/Cookies', 'Default/Login Data', 'Default/Web Data', 'Default/Local Storage',
#              'Default/IndexedDB', 'Default/Extension State', 'Default/Extensions', 'Local State']
#     for item in items:
#         s, d = os.path.join(src, item), os.path.join(dst, item)
#         if os.path.exists(s):
#             os.makedirs(os.path.dirname(d), exist_ok=True)
#             try:
#                 if os.path.isdir(s):
#                     if os.path.exists(d): shutil.rmtree(d)
#                     shutil.copytree(s, d)
#                 else:
#                     shutil.copy2(s, d)
#             except: pass
#     print(f"  → Synced profile data from {src}")
# ─── END EXPERIMENTAL ─────────────────────────────────────────

def check_cdp_available(timeout=10):
    """Check if CDP endpoint is ready (library glue)"""
    import urllib.request
    import json
    start = time.time()
    print(f"  → Checking CDP at http://localhost:9222/json (timeout: {timeout}s)...")
    while time.time() - start < timeout:
        try:
            response = urllib.request.urlopen('http://localhost:9222/json', timeout=1)
            targets = json.loads(response.read())
            print(f"  → CDP response: {len(targets)} targets found")
            if any(t.get('type') == 'page' for t in targets):
                print(f"  ✓ CDP available with page targets")
                return True
            print(f"  → No page targets yet, retrying...")
        except Exception as e:
            pass
        time.sleep(0.05)
    print(f"  ✗ CDP timeout after {timeout}s")
    return False

def _launch_chrome_positioned():
    """Launch Chrome with positioning, return after CDP ready (no Playwright connection)"""
    global _chrome_process, _profile_dir

    user_data_dir = get_profile_path()
    _profile_dir = user_data_dir
    if IS_MAC:
        print(f"  → Profile: {user_data_dir}")
    else:
        v, b = _automation_choice()
        print(f"  → Automation: chrome-{v} profile via {os.path.basename(b)} ({user_data_dir})")
    _terminate_profile_processes(user_data_dir)
    os.makedirs(user_data_dir, exist_ok=True)
    time.sleep(0.5)

    target_monitor = _get_target_monitor()
    if target_monitor:
        mode_name = "primary/center" if _center_mode else "rightmost"
        print(f"  → Target: {mode_name} monitor at ({target_monitor['x']},{target_monitor['y']})")

    chrome_args, env = _prepare_chrome_launch(user_data_dir)

    if IS_SWAY:
        # Launch via swaymsg exec so Chrome inherits Sway's compositor env
        cmd_str = ' '.join(f"'{a}'" if ' ' in a or '=' in a else a for a in chrome_args)
        _swaymsg('exec', cmd_str)
        _chrome_process = None  # can't track PID from swaymsg
        print(f"  → Chrome launched via swaymsg exec")
    else:
        _chrome_process = subprocess.Popen(chrome_args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"  → Chrome process PID: {_chrome_process.pid}")
    if IS_SWAY:
        print(f"  → WAYLAND_DISPLAY={env.get('WAYLAND_DISPLAY')} (Sway)")
    elif not IS_MAC:
        print(f"  → DISPLAY={env.get('DISPLAY')}")

    _position_chrome_window(target_monitor)

    print("  → Waiting for CDP...")
    if not check_cdp_available(timeout=10):
        raise RuntimeError("CDP not available")

def _cdp_new_page():
    """Create CDPPage — create fresh tab via Target.createTarget (skips extension init delay)"""
    import urllib.request, json, websocket as _ws
    # Connect to browser-level websocket
    r = urllib.request.urlopen('http://localhost:9222/json/version', timeout=5)
    info = json.loads(r.read())
    bws = _ws.create_connection(info['webSocketDebuggerUrl'], timeout=10)
    bws.send(json.dumps({'id': 1, 'method': 'Target.createTarget', 'params': {'url': 'about:blank'}}))
    resp = json.loads(bws.recv())
    tid = resp.get('result', {}).get('targetId')
    bws.close()
    if not tid:
        # Fallback: use existing page
        r = urllib.request.urlopen('http://localhost:9222/json', timeout=5)
        targets = json.loads(r.read())
        target = next((t for t in targets if t.get('type') == 'page'), None)
        if not target: raise RuntimeError("No page target found")
        ws_url = target['webSocketDebuggerUrl']
    else:
        # Get websocket URL for new target
        r = urllib.request.urlopen('http://localhost:9222/json', timeout=5)
        targets = json.loads(r.read())
        target = next((t for t in targets if t.get('id') == tid), None)
        ws_url = target['webSocketDebuggerUrl'] if target else None
        if not ws_url: raise RuntimeError(f"Target {tid} not found")
    print(f"  → CDP direct: {ws_url[:60]}")
    return CDPPage(ws_url)

def _cdp_already_running():
    """Check if a browser with CDP is already listening on 9222"""
    import urllib.request
    try:
        urllib.request.urlopen('http://localhost:9222/json', timeout=1)
        return True
    except: return False

def launch_browser_with_positioning():
    """Connect to existing browser or launch one. CDP by default, --pw for playwright."""
    global _playwright, _browser, _browser_context, _page

    if _page and isinstance(_page, CDPPage):
        try: _page.close()
        except: pass
    if _browser:
        try: _browser.close()
        except: pass
    if _playwright:
        try: _playwright.stop()
        except: pass

    if _use_cdp:
        # Hot path: if browser already running, just create tab — ~20ms
        if _cdp_already_running():
            _page = _cdp_new_page()
            print(f"  ✓ CDP page ready (existing browser)")
            return None
        # Cold path: launch browser first
        _launch_chrome_positioned()
        _page = _cdp_new_page()
        print(f"  ✓ CDP page ready")
        return None

    if not _HAS_PLAYWRIGHT:
        raise RuntimeError("Playwright not installed. Use CDP mode (default) or: pip install playwright")

    _playwright = sync_playwright().start()
    _browser = _playwright.chromium.connect_over_cdp('http://localhost:9222')
    _browser_context = _browser.contexts[0] if _browser.contexts else None

    if not _browser_context:
        raise RuntimeError("No browser context")

    print(f"  → Found {len(_browser_context.pages)} existing page(s)")

    user_pages = [p for p in _browser_context.pages
                  if p.url and not p.url.startswith(('chrome://', 'chrome-extension://', 'devtools://'))]

    print(f"  → {len(user_pages)} user page(s) (excluding internal Chrome pages)")

    print(f"  → Creating fresh page")
    _page = _browser_context.new_page()
    print(f"  ✓ Fresh page created: {_page.url}")

    for page in user_pages:
        try:
            print(f"  → Closing old page: {page.url[:60] if page.url else 'unknown'}")
            page.close()
        except Exception as e:
            print(f"  → Failed to close page: {str(e)[:40]}")

    return _browser_context

def get_page():
    """Get current page (library glue)"""
    return _page

def close_browser():
    """Close browser and cleanup (library glue)"""
    global _playwright, _browser, _browser_context, _page, _chrome_process, _profile_dir

    if _page and isinstance(_page, CDPPage):
        try: _page.close()
        except: pass
    elif _browser:
        try: _browser.close()
        except: pass
    if _playwright:
        try: _playwright.stop()
        except: pass
    if _chrome_process:
        try:
            _chrome_process.terminate()
            _chrome_process.wait(timeout=5)
        except:
            _chrome_process.kill()
    _chrome_process = None

    if _profile_dir:
        _terminate_profile_processes(_profile_dir)

    _browser = None
    _browser_context = None
    _page = None

atexit.register(close_browser)

# ═══════════════════════════════════════════════════════════
# PAGE INTERACTIONS - Pure Playwright library glue
# ═══════════════════════════════════════════════════════════

def navigate_to(url):
    """Navigate to URL (library glue)"""
    get_page().goto(url, wait_until='domcontentloaded')
    time.sleep(3)

def get_current_url():
    """Get current page URL (library glue)"""
    try:
        return get_page().url
    except:
        return None

def is_page_blank():
    """Check if page is about:blank (library glue)"""
    try:
        url = get_page().url
        return not url or url.startswith('about:')
    except:
        return True

def wait_for_page_loaded(timeout=30):
    """Wait for page to be fully loaded with multiple strategies (library glue)"""
    try:
        page = get_page()

        # Strategy 1: Wait for load event
        page.wait_for_load_state('load', timeout=timeout * 1000)

        # Strategy 2: Wait for network to be idle (no more than 2 connections for 500ms)
        page.wait_for_load_state('networkidle', timeout=10000)

        # Strategy 3: Extra wait for dynamic content
        time.sleep(1)

        return True
    except Exception as e:
        print(f"  ⚠ Page load wait timeout: {str(e)[:50]}")
        return False

def navigate_to_robust(url, max_retries=3, verify_url=True):
    """Navigate to URL with verification and retry logic (library glue)"""
    page = get_page()

    for attempt in range(max_retries):
        try:
            print(f"  → Navigation attempt {attempt + 1}/{max_retries}: {url}")

            page.goto(url, wait_until='domcontentloaded', timeout=30000)
            try:
                page.wait_for_selector('body', timeout=2500)
                page.wait_for_selector('img, a, button', timeout=2500)
            except:
                print("  → Proceeding (page probably loaded)")
            time.sleep(1)

            current_url = page.url
            print(f"  → Current URL: {current_url}")

            # Verify domain if requested
            if verify_url:
                from urllib.parse import urlparse
                target_domain = urlparse(url).netloc
                current_domain = urlparse(current_url).netloc if current_url else None

                if not current_domain or target_domain not in current_domain:
                    print(f"  ⚠ Domain mismatch: expected {target_domain}, got {current_domain}")
                    if attempt < max_retries - 1:
                        time.sleep(2 ** attempt)
                        continue
                    return (False, current_url)

            print(f"  ✓ Navigation successful: {current_url}")
            return (True, current_url)

        except Exception as e:
            print(f"  ⚠ Navigation error (attempt {attempt + 1}): {str(e)[:60]}")
            if attempt < max_retries - 1:
                time.sleep(2 ** attempt)
            else:
                return (False, get_current_url())

    return (False, None)

def navigate_with_search_fallback(query=None, url=None, search_engines=None):
    """
    Navigate to URL or search engines with fallback (library glue)

    Priority order (as requested):
    1. Bing
    2. Google
    3. DuckDuckGo
    4. Yahoo
    5. Brave Search

    Args:
        query: Search query string (optional)
        url: Direct URL to try first (optional)
        search_engines: Custom list of search engines (optional)

    Returns:
        (success, final_url, engine_name)
    """
    if search_engines is None:
        # Default search engines in priority order (Bing first as requested)
        search_engines = [
            ('Bing', 'https://www.bing.com'),
            ('Google', 'https://www.google.com'),
            ('DuckDuckGo', 'https://duckduckgo.com'),
            ('Yahoo', 'https://www.yahoo.com'),
            ('Brave', 'https://search.brave.com')
        ]

    # Try direct URL first if provided
    if url:
        print(f"\n  → Trying direct URL: {url}")
        success, final_url = navigate_to_robust(url, max_retries=2)
        if success:
            return (True, final_url, 'Direct')
        else:
            print(f"  ✗ Direct URL failed: {url}")

    # Try each search engine in order
    for engine_name, engine_url in search_engines:
        print(f"\n  → Trying {engine_name}: {engine_url}")

        # Build search URL if query provided
        if query:
            if 'bing.com' in engine_url:
                search_url = f"{engine_url}/search?q={query}"
            elif 'google.com' in engine_url:
                search_url = f"{engine_url}/search?q={query}"
            elif 'duckduckgo.com' in engine_url:
                search_url = f"{engine_url}/?q={query}"
            elif 'yahoo.com' in engine_url:
                search_url = f"{engine_url}/search?p={query}"
            elif 'brave.com' in engine_url:
                search_url = f"{engine_url}/search?q={query}"
            else:
                search_url = engine_url
        else:
            search_url = engine_url

        success, final_url = navigate_to_robust(search_url, max_retries=2)

        if success:
            print(f"  ✓ {engine_name} loaded successfully!")
            return (True, final_url, engine_name)
        else:
            print(f"  ✗ {engine_name} failed, trying next...")

    # All engines failed
    print(f"\n  ✗ All search engines failed")
    return (False, None, None)

def take_screenshot(path):
    """Take screenshot using Playwright (library glue)"""
    try:
        get_page().screenshot(path=path, full_page=False, animations='disabled', timeout=60000)
    except Exception as e:
        print(f"  ⚠ Screenshot failed: {e}")

# ═══════════════════════════════════════════════════════════
# ASYNC PAGE NAVIGATION - Robust async navigation with retry
# ═══════════════════════════════════════════════════════════

async def get_current_url_async(page):
    """Get current page URL (library glue - async)"""
    try:
        return page.url
    except:
        return None

async def is_page_blank_async(page):
    """Check if page is about:blank (library glue - async)"""
    try:
        url = page.url
        return not url or url.startswith('about:')
    except:
        return True

async def wait_for_page_loaded_async(page, timeout=30):
    """Wait for page to be fully loaded with multiple strategies (library glue - async)

    AGGRESSIVE TIMEOUTS - Fail fast, retry more
    """
    try:
        # Strategy 1: Wait for load event (AGGRESSIVE: reduced from 30s to timeout param)
        await page.wait_for_load_state('load', timeout=timeout * 1000)

        # Strategy 2: Wait for network to be idle (AGGRESSIVE: reduced from 10s to 6s)
        try:
            await page.wait_for_load_state('networkidle', timeout=6000)
        except:
            # Network might never be idle for some sites, continue anyway
            pass

        # Strategy 3: Extra wait for dynamic content (AGGRESSIVE: reduced from 1s to 0.5s)
        await asyncio.sleep(0.5)

        return True
    except Exception as e:
        print(f"  ⚠ Page load wait timeout: {str(e)[:50]}")
        return False

async def navigate_to_robust_async(page, url, max_retries=3, verify_url=True, aggressive=True):
    """Navigate to URL with verification and retry logic (library glue - async)"""
    for attempt in range(max_retries):
        try:
            print(f"  → [{url}] Navigation attempt {attempt + 1}/{max_retries}")

            timeout_ms = 15000 if aggressive else 30000
            await page.goto(url, wait_until='domcontentloaded', timeout=timeout_ms)

            try:
                await page.wait_for_selector('body', timeout=2500)
                await page.wait_for_selector('img, a, button', timeout=2500)
            except:
                print("  → Proceeding (page probably loaded)")

            await asyncio.sleep(0.5)

            current_url = page.url
            print(f"  → Current URL: {current_url}")

            # Verify domain if requested
            if verify_url:
                from urllib.parse import urlparse
                target_domain = urlparse(url).netloc
                current_domain = urlparse(current_url).netloc if current_url else None

                if not current_domain or target_domain not in current_domain:
                    print(f"  ⚠ Domain mismatch: expected {target_domain}, got {current_domain}")
                    if attempt < max_retries - 1:
                        await asyncio.sleep(0.5 * (2 ** attempt) if aggressive else 2 ** attempt)
                        continue
                    return (False, current_url)

            print(f"  ✓ Navigation successful: {current_url}")
            return (True, current_url)

        except Exception as e:
            print(f"  ⚠ Navigation error (attempt {attempt + 1}): {str(e)[:60]}")
            if attempt < max_retries - 1:
                await asyncio.sleep(0.5 * (2 ** attempt) if aggressive else 2 ** attempt)
            else:
                return (False, page.url)

    return (False, None)

async def ensure_page_visible(page, max_retries=3):
    """Ensure page is visible and has proper dimensions (library glue - async)"""
    for attempt in range(max_retries):
        try:
            # Bring page to front
            await page.bring_to_front()
            await asyncio.sleep(0.5)

            # Check viewport size
            viewport = page.viewport_size
            if viewport and viewport.get('width', 0) > 0 and viewport.get('height', 0) > 0:
                print(f"  ✓ Page visible: {viewport['width']}x{viewport['height']}")
                return True

            # Set viewport if not set
            await page.set_viewport_size({'width': 1400, 'height': 900})
            await asyncio.sleep(0.5)

            # Verify again
            viewport = page.viewport_size
            if viewport and viewport.get('width', 0) > 0:
                return True

            print(f"  ⚠ Page visibility attempt {attempt + 1}/{max_retries} failed")
            await asyncio.sleep(0.5)

        except Exception as e:
            print(f"  ⚠ Visibility check error (attempt {attempt + 1}): {str(e)[:50]}")
            await asyncio.sleep(0.5)

    return False

async def wait_for_page_fully_rendered(page, platform_name, timeout=15):
    """
    Wait for page to be fully rendered with platform-specific checks (library glue - async)

    More aggressive timeouts with specific detection per platform
    """
    try:
        # Step 1: Ensure page is visible
        if not await ensure_page_visible(page):
            print(f"  ⚠ [{platform_name}] Page not visible after retries")
            return False

        # Step 2: Wait for initial load (AGGRESSIVE: 8s timeout)
        try:
            await page.wait_for_load_state('domcontentloaded', timeout=8000)
        except:
            print(f"  ⚠ [{platform_name}] DOM load timeout (8s)")
            # Continue anyway, might still work

        # Step 3: Platform-specific element detection (AGGRESSIVE: 5s timeout)
        platform_selectors = {
            'DeepSeek': ['textarea', 'div[contenteditable="true"]', 'input[type="text"]', '.chat-input'],
            'Claude': ['div[contenteditable="true"]', 'fieldset'],
            'Grok': ['div[contenteditable="true"]', 'textarea'],
            'ChatGPT': ['div[contenteditable="true"]', '#prompt-textarea'],
            'Gemini': ['div[role="textbox"]', 'textarea'],
            'Perplexity': ['div[contenteditable="true"]', 'textarea'],
            'Qwen': ['textarea', 'div[contenteditable="true"]']
        }

        selectors = platform_selectors.get(platform_name, ['textarea', 'div[contenteditable="true"]'])

        for selector in selectors:
            try:
                await page.wait_for_selector(selector, state='visible', timeout=5000)
                print(f"  ✓ [{platform_name}] Found input: {selector}")

                # Extra wait for dynamic content to settle (AGGRESSIVE: only 1s)
                await asyncio.sleep(1)
                return True
            except:
                continue

        print(f"  ⚠ [{platform_name}] No input elements found (might still work)")
        # Still return True, we'll try to interact anyway
        await asyncio.sleep(2)
        return True

    except Exception as e:
        print(f"  ⚠ [{platform_name}] Render wait error: {str(e)[:60]}")
        return False

async def screenshot_with_retry(page, path, max_retries=3):
    """Take screenshot with retry logic (library glue - async)"""
    for attempt in range(max_retries):
        try:
            # Ensure page is visible before screenshot
            await ensure_page_visible(page)
            await asyncio.sleep(0.3)

            # Take screenshot
            await page.screenshot(path=path)
            return True

        except Exception as e:
            error_msg = str(e)
            if '0 width' in error_msg or '0 height' in error_msg:
                print(f"  ⚠ Screenshot attempt {attempt + 1}/{max_retries}: Zero dimensions")
                # Try to fix by setting viewport and bringing to front
                try:
                    await page.set_viewport_size({'width': 1400, 'height': 900})
                    await page.bring_to_front()
                    await asyncio.sleep(0.5)
                except:
                    pass
            else:
                print(f"  ⚠ Screenshot attempt {attempt + 1}/{max_retries}: {error_msg[:50]}")

            if attempt < max_retries - 1:
                await asyncio.sleep(0.5)
            else:
                print(f"  ✗ Screenshot failed after {max_retries} attempts")
                return False

    return False

def check_text_exists(text):
    """Check if text exists on page (library glue)"""
    try:
        return get_page().get_by_text(text, exact=False).count() > 0
    except:
        return False

def check_element_exists(selector):
    """Check if element exists (library glue)"""
    return get_page().query_selector(selector) is not None

def click_element(selector, timeout=5000):
    """Click element by selector (library glue)"""
    try:
        get_page().click(selector, timeout=timeout)
        return True
    except:
        return False

def click_text(text):
    """Click element containing text (library glue)"""
    try:
        get_page().get_by_text(text, exact=False).first.click(timeout=5000)
        return True
    except:
        return False

def fill_input(selector, text):
    """Fill input field (library glue)"""
    try:
        get_page().locator(selector).fill(text, timeout=5000)
        return True
    except:
        return False

def press_key(key):
    """Press keyboard key (library glue)"""
    get_page().keyboard.press(key)

def type_text(text):
    """Type text (library glue)"""
    get_page().keyboard.type(text)

# ═══════════════════════════════════════════════════════════
# WORKFLOWS - Library glue composition with window management
# ═══════════════════════════════════════════════════════════

def google_workflow():
    """Google verification workflow with window positioning (library glue)"""
    print("═" * 50)
    print("Google Verification (Wayland + XWayland Tools)")
    print("═" * 50 + "\n")

    print("  → Launching browser with positioning...")
    launch_browser_with_positioning()

    print("  → Navigating to Google...")
    success, final_url = navigate_to_robust('https://google.com', max_retries=3)
    if not success:
        print("  ⚠ Failed to load Google, trying search engine fallback...")
        success, final_url, engine = navigate_with_search_fallback(url='https://google.com')
        if not success:
            print("  ✗ All navigation attempts failed")
            return
        print(f"  ✓ Loaded via {engine}: {final_url}")

    print("  → Checking sign-in status...")
    has_signin = check_text_exists('Sign in')
    is_signed_in = check_element_exists('a[aria-label*="Google Account"]')

    screenshot_path = '/tmp/google_wayland.png'
    print(f"  → Screenshot: {screenshot_path}")
    take_screenshot(screenshot_path)

    print("\n" + "═" * 50)
    print(f"  Status: {'✓ SIGNED IN' if is_signed_in else '✗ NOT SIGNED IN'}")
    print(f"  Screenshot: {screenshot_path}")
    print("═" * 50)

    if not is_signed_in:
        print("\n  → Sign in manually if needed...")
        print("  → Press Ctrl+C when done\n")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n✓ Session saved")

def _kill_old_sessions():
    """Kill any stale automation processes (library glue)"""
    profile_pattern = 'agui/chrome-profile' if IS_MAC else 'chrome-profile-wayland'
    subprocess.run(['pkill', '-f', profile_pattern], capture_output=True)
    subprocess.run(['pkill', '-f', 'playwright.*run-driver'], capture_output=True)
    time.sleep(0.5)

def extract_response_by_answer_tags(text, query=None):
    """Find all <answer>...</answer> tags in text, return all matches.
    Handles both raw tags and HTML-escaped tags (&lt;answer&gt;).
    For HTML-escaped matches, strips internal HTML markup (span, comments etc).
    Last match is the real response (thinking text may produce earlier ones).
    If query provided, filters out matches containing the query (false positives).
    Returns (list_of_matches, success_bool). Fast, no subprocess."""
    import re
    if not text:
        return ([], False)
    # Raw tags (innerText, clipboard)
    matches = re.findall(r'<answer>(.*?)</answer>', text, re.DOTALL)
    # HTML-escaped tags (raw HTML from React apps like Claude/AIStudio)
    html_matches = re.findall(r'&lt;answer&gt;(.*?)&lt;/answer&gt;', text, re.DOTALL)
    for m in html_matches:
        # Strip HTML tags and comments that get between characters in rendered HTML
        cleaned = re.sub(r'<[^>]+>', '', m)
        cleaned = re.sub(r'<!--.*?-->', '', cleaned)
        cleaned = cleaned.replace('&lt;', '<').replace('&gt;', '>').replace('&amp;', '&').replace('&quot;', '"').replace('&#39;', "'")
        matches.append(cleaned)
    matches = [m.strip() for m in matches if m.strip()]
    # Filter out false positives that contain the query text (thinking+prompt combos)
    if query and matches:
        q_snippet = query[:40]
        matches = [m for m in matches if q_snippet not in m]
    return (matches, True) if matches else ([], False)


def extract_response_via_claude(inner_text, platform_name, query=None):
    """Fallback: send rendered page text to Claude CLI to extract the LLM response.
    Operates on innerText (not raw HTML) for cleaner, smaller input.
    Returns (extracted_text, success_bool). Uses <response> tags for clean parsing."""
    import re

    if not inner_text or len(inner_text.strip()) < 2:
        return (None, False)

    # Truncate to stay within context (innerText is much smaller than HTML)
    max_len = 80000
    truncated = inner_text[:max_len] if len(inner_text) > max_len else inner_text

    query_hint = f'\nThe user\'s query was: "{query[:200]}"' if query else ''

    prompt = (
        f"You are given the visible text of {platform_name}'s chat page (extracted via innerText). "
        f"This includes sidebar text, navigation, and the actual chat conversation.{query_hint}\n"
        f"Extract ONLY the LLM's response text (not the user's prompt, not UI elements, not navigation, not sidebar text, not boilerplate). "
        f"If there is no LLM response in the text, reply with <response>NO_RESPONSE</response>. "
        f"Otherwise reply with the response wrapped in tags: <response>the extracted text here</response>. "
        f"Do NOT include any other text outside the tags.\n\n"
        f"--- BEGIN PAGE TEXT ---\n{truncated}\n--- END PAGE TEXT ---"
    )

    try:
        result = subprocess.run(
            ['claude', '-p', '--dangerously-skip-permissions', '--model', 'haiku'],
            input=prompt, capture_output=True, text=True, timeout=120
        )

        if result.returncode != 0:
            return (None, False)

        match = re.search(r'<response>(.*?)</response>', result.stdout, re.DOTALL)
        if match:
            text = match.group(1).strip()
            if text == 'NO_RESPONSE':
                return (None, False)
            return (text, True)

        # No tags found — use raw stdout if non-trivial
        stdout = result.stdout.strip()
        if stdout and len(stdout) > 2:
            return (stdout, True)
        return (None, False)

    except subprocess.TimeoutExpired:
        return (None, False)
    except Exception:
        return (None, False)


# ═══════════════════════════════════════════════════════════
# FILE UPLOAD - Platform-specific file upload (library glue)
# ═══════════════════════════════════════════════════════════

# Platforms where file upload blocks on non-text files (text extraction only)
_text_only_upload = {'DeepSeek', 'Ernie'}

async def upload_files_to_page(page, name, file_paths):
    """Upload files to a platform page. Returns True if successful (library glue - async).
    Uses set_input_files on hidden <input type=file> (most platforms),
    or menu->filechooser intercept (Gemini)."""
    if not file_paths:
        return False

    # Filter non-text files for text-only platforms
    paths = list(file_paths)
    if name in _text_only_upload:
        _text_exts = {'.txt','.md','.csv','.json','.py','.js','.ts','.html','.css','.java',
                      '.c','.cpp','.h','.go','.rs','.rb','.php','.sql','.xml','.yaml','.yml',
                      '.toml','.ini','.conf','.log','.sh','.bat','.pdf','.doc','.docx','.pptx','.xlsx'}
        paths = [p for p in paths if os.path.splitext(p)[1].lower() in _text_exts]
        if not paths:
            print(f"  ⚠ [{name}] No text-extractable files to upload")
            return False

    try:
        # Method 1: Gemini needs menu -> "Upload files" -> filechooser
        if name == 'Gemini':
            btn = page.locator('button[aria-label="Open upload file menu"]')
            await btn.click(timeout=3000)
            await asyncio.sleep(1)
            async with page.expect_file_chooser(timeout=5000) as fc_info:
                await page.locator('text=/upload file/i').first.click(timeout=3000)
            fc = await fc_info.value
            await fc.set_files(paths)
            print(f"  ✓ [{name}] Uploaded {len(paths)} file(s) via menu->filechooser")
            await asyncio.sleep(2)
            return True

        # Method 2: Direct set_input_files on hidden <input type=file> (most platforms)
        fi_count = await page.evaluate("document.querySelectorAll('input[type=\"file\"]').length")
        if fi_count > 0:
            await page.locator('input[type="file"]').first.set_input_files(paths)
            print(f"  ✓ [{name}] Uploaded {len(paths)} file(s) via set_input_files")
            await asyncio.sleep(2)
            return True

        # Method 3: Click attach button -> filechooser
        for sel in ['button[aria-label*="ttach" i]', 'button[aria-label*="pload" i]',
                     'button[aria-label*="ile" i]', 'button[aria-label*="dd file" i]']:
            try:
                loc = page.locator(sel).first
                if await loc.is_visible(timeout=1000):
                    async with page.expect_file_chooser(timeout=5000) as fc_info:
                        await loc.click()
                    fc = await fc_info.value
                    await fc.set_files(paths)
                    print(f"  ✓ [{name}] Uploaded {len(paths)} file(s) via filechooser")
                    await asyncio.sleep(2)
                    return True
            except:
                continue

        print(f"  ⚠ [{name}] No upload mechanism found")
        return False

    except Exception as e:
        print(f"  ⚠ [{name}] Upload failed: {str(e)[:60]}")
        return False


async def multi_ai_async(query=None, _tabs=1, only=None, files=None):
    """Multi-AI platforms workflow - FAST parallel with positioning (library glue - async)"""
    print("═" * 50)
    print("Multi-AI Platforms (Fast Parallel)")
    print("═" * 50 + "\n")

    # Generate query if not provided (library glue)
    if not query:
        from wordfreq import top_n_list
        words = top_n_list('en', 10000)
        word = random.choice(words)
        response = requests.post('http://localhost:11434/api/generate', json={
            'model': 'gemma3',
            'prompt': f'Create a coding task with "{word}". Output: "Write Python code to [task]"',
            'stream': False
        })
        query = response.json().get('response', word).strip().strip('"').strip("'")

    # Append answer tag instruction for reliable extraction
    _answer_tag_suffix = ' Wrap your answer in <answer></answer> tags.'
    tagged_query = query + _answer_tag_suffix

    print(f"  → Query: {query}\n")

    # Launch Chrome with positioning (uses same logic as deep research)
    _launch_chrome_positioned()

    # Connect async Playwright (library glue)
    playwright = await async_playwright().start()
    browser = await playwright.chromium.connect_over_cdp('http://localhost:9222')
    context = browser.contexts[0]

    # Reuse existing page for first platform, no placeholder needed
    existing = [p for p in context.pages if not p.url.startswith(('chrome://', 'chrome-extension://'))]
    first_page = existing[0] if existing else await context.new_page()

    # AI platforms - FAST parallel loading (library glue)
    platforms = [
        ('DeepSeek', 'https://chat.deepseek.com/'),
        ('Claude', 'https://claude.ai/'),
        ('Grok', 'https://grok.com/'),
        ('ChatGPT', 'https://chatgpt.com/'),
        ('Gemini', 'https://gemini.google.com/u/0/app'),
        ('Perplexity', 'https://www.perplexity.ai/'),
        ('Qwen', 'https://chat.qwen.ai/'),
        ('AIStudio', 'https://aistudio.google.com/app/prompts/new_chat?model=gemini-3-pro-preview'),
        ('Ernie', 'https://ernie.baidu.com/'),
        ('Kimi', 'https://www.kimi.com/'),
        ('Zai', 'https://chat.z.ai/'),
    ]
    if only:
        only_list = [o.strip().lower() for o in (only.split(',') if isinstance(only, str) else only)]
        platforms = [(n,u) for n,u in platforms if n.lower() in only_list]

    # Grant clipboard permissions so navigator.clipboard.readText() works after copy button click
    for _, url in platforms:
        try: await context.grant_permissions(['clipboard-read', 'clipboard-write'], origin=url.rstrip('/'))
        except: pass

    print(f"\nOpening {len(platforms)} platforms sequentially (parallel goto overwhelms Chrome)...\n")

    async def open_platform(name, url, page=None):
        try:
            page = page or await context.new_page()
            await page.goto(url, wait_until='domcontentloaded', timeout=30000)
            print(f"  ✓ [{name}] Loaded")
            return (name, page, True)
        except Exception as e:
            print(f"  ✗ [{name}] {str(e)[:40]}")
            return (name, page, False)

    results = [await open_platform(n, u, first_page if j == 0 else None) for j, (n, u) in enumerate(platforms)]
    pages = [(n, p) for n, p, ok in results if p is not None]
    failed = [n for n, p, ok in results if p is None]
    print(f"\n✓ {len(pages)}/{len(platforms)} tabs loaded")
    if failed:
        print(f"  ✗ Completely failed (no page): {', '.join(failed)}")

    # Platform-specific input selectors (library glue)
    input_selectors = {
        'Gemini': ['div[role="textbox"]', 'div[contenteditable="true"]', 'textarea'],
        'Qwen': ['textarea', 'div[contenteditable="true"]'],
        'DeepSeek': ['textarea', 'div[contenteditable="true"]', 'input[type="text"]'],
        'Ernie': ['div[role="textbox"][data-slate-editor="true"]', 'div[role="textbox"]', 'div[contenteditable="true"]'],
        'Kimi': ['div[data-lexical-editor="true"]', 'div.chat-input-editor', 'div[contenteditable="true"]'],
        'Zai': ['textarea', 'div[contenteditable="true"]', 'div[role="textbox"]'],
        'default': ['textarea', 'div[contenteditable="true"]', 'input[type="text"]']
    }

    # Platforms that use rich-text editors (Lexical/Slate) — must use keyboard.type(), not fill()
    _keyboard_type_platforms = {'Ernie', 'Kimi'}
    # Platforms where thinking text produces false positive <answer> tag matches
    _skip_answer_tag_wait = {'Ernie'}

    # Data logging directory (library glue)
    from datetime import datetime as _dt
    _run_ts = _dt.now().strftime('%Y%m%d_%H%M%S')
    _data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'adata', 'local', 'agui', _run_ts)
    os.makedirs(_data_dir, exist_ok=True)
    print(f"  → Logging to: {_data_dir}\n")

    # Send query with response detection (library glue - asyncio.gather)
    async def send_and_get_response(name, page, _r=3):
        """Send query, wait for response, extract via copy button (library glue)"""
        # Per-platform data dir
        _platform_dir = os.path.join(_data_dir, name.lower())
        os.makedirs(_platform_dir, exist_ok=True)

        def _save(filename, content):
            """Save content to platform data dir, return path"""
            p = os.path.join(_platform_dir, filename)
            with open(p, 'w') as f: f.write(content if content else '')
            return p

        try:
            print(f"\n  → [{name}] Sending query...")

            # Quick check - should not be blank with fresh pages
            current_url = page.url
            if not current_url or 'about:blank' in current_url:
                print(f"  ✗ [{name}] Page is blank, skipping")
                _save('meta.txt', f"URL: {current_url}\nSTATUS: blank_page\n")
                return (name, None, None, None)

            print(f"  → [{name}] Current URL: {current_url}")

            # Wait for JS frameworks to mount input elements (library glue)
            await asyncio.sleep(3)

            # Upload files if provided (library glue)
            if files:
                await upload_files_to_page(page, name, files)

            # Get selectors for this platform (library glue)
            selectors = input_selectors.get(name, input_selectors['default'])
            query_sent = False
            for sel in selectors:
                try:
                    # Wait for element to exist before clicking
                    await page.wait_for_selector(sel, timeout=5000, state='visible')
                    elem = page.locator(sel).first
                    if name in _keyboard_type_platforms:
                        # Rich-text editors (Lexical/Slate): focus via JS then use keyboard events
                        await elem.evaluate('el => el.focus()')
                        await asyncio.sleep(0.3)
                        await page.keyboard.press('Meta+a')
                        await page.keyboard.press('Backspace')
                        await page.keyboard.type(tagged_query, delay=10)
                    else:
                        await elem.click(timeout=3000)
                        await asyncio.sleep(0.3)
                        if name == 'Perplexity':
                            words = tagged_query.split(' ', 1)
                            await elem.type(words[0] + ' ', delay=50)
                            if len(words) > 1:
                                await elem.fill(words[0] + ' ' + words[1])
                        else:
                            await elem.fill(tagged_query, timeout=2000)
                    await page.keyboard.press('Enter')
                    query_sent = True
                    print(f"  ✓ [{name}] Query sent with selector: {sel}")
                    break
                except:
                    continue

            if not query_sent:
                print(f"  ✗ [{name}] Could not find input field" + (f", retrying ({_r-1} left)" if _r > 1 else ""))
                _save('meta.txt', f"URL: {current_url}\nSTATUS: input_failed\n")
                return await send_and_get_response(name, page, _r-1) if _r > 1 else (name, None, None, None)

            # Wait for response - detect via <answer> tags in page OR copy button (0.5s polling)
            print(f"  → [{name}] Waiting for response...")
            response_ready = False
            _q_snippet = query[:40].replace("'", "\\'").replace("\\", "\\\\")
            for attempt in range(120):  # 60s max
                await asyncio.sleep(0.5)
                try:
                    # <answer> tag INSIDE an assistant container (skip thinking-mode noise in body)
                    if name not in _skip_answer_tag_wait and attempt >= 10:
                        has_answer = await page.evaluate(f"""(()=>{{const A=document.querySelectorAll('[data-message-author-role="assistant"],message-content,.font-claude-message,.ds-markdown,.segment-assistant,.message-text,.markdown-body,.prose,#answer_text_id,.response-content');if(!A.length)return false;for(const a of A){{const t=a.innerText||'',h=a.innerHTML||'';const m=t.match(/<answer>([\\s\\S]+?)<\\/answer>/);if(m&&m[1].indexOf('{_q_snippet}')===-1)return true;const m2=h.match(/&lt;answer&gt;((?:(?!&lt;answer&gt;)[\\s\\S])+?)&lt;\\/answer&gt;/);if(m2&&m2[1].indexOf('{_q_snippet}')===-1)return true}}return false}})()""")
                        if has_answer:
                            response_ready = True
                            print(f"  ✓ [{name}] <answer> in assistant element at {attempt*0.5:.0f}s")
                            break
                    # Copy button INSIDE assistant container (not user msg) — fires only on real response
                    if attempt >= 6:
                        if await page.evaluate("""(()=>{const A=document.querySelectorAll('[data-message-author-role="assistant"],message-content,.font-claude-message,.ds-markdown,.segment-assistant,.message-text,.prose,.markdown-body,#answer_text_id');for(const a of A)if(a.querySelector('button[aria-label*="opy"],#copy-container,svg[name="Copy"],[class*="copy"]'))return true;return false})()"""):
                            response_ready = True
                            print(f"  ✓ [{name}] Assistant copy button at {attempt*0.5:.0f}s")
                            break
                except:
                    pass

            if not response_ready:
                print(f"  ⚠ [{name}] Timeout waiting for response")

            # Capture page state AFTER response is ready (library glue)
            await asyncio.sleep(1)  # brief settle time
            raw_html = await page.content()
            inner_text = await page.evaluate("document.body.innerText")
            url_path = _save('url.txt', current_url)
            html_path = _save('raw.html', raw_html)
            _save('innertext.txt', inner_text or '')
            await page.screenshot(path=os.path.join(_platform_dir, 'response.png'))

            # Find and click copy button (library glue)
            print(f"  → [{name}] Finding copy button...")
            copied = False

            # Platform-specific copy button selectors (library glue)
            copy_map = {
                'Qwen': ['button:has(svg[class*="copy"])', '[class*="copy-btn"]', 'button:has-text("复制")'],
                'DeepSeek': ['button:has(svg)', '[class*="copy"]', 'button[class*="ds-"]'],
                'Ernie': ['[class*="copy"]:has(svg)', 'svg path[d^="M11.7 4.604"]'],
                'Kimi': ['.icon-button:has(svg[name="Copy"])', '.segment-assistant-actions .icon-button:first-child'],
            }
            copy_selectors = copy_map.get(name, ['button[aria-label*="opy"]', '[data-testid*="copy"]', 'button:has-text("Copy")'])

            for selector in copy_selectors:
                try:
                    buttons = page.locator(selector)
                    count = await buttons.count()
                    if count > 0:
                        print(f"  → [{name}] Found {count} buttons with: {selector}")

                        # Try buttons in reverse order (last = most recent response) (library glue)
                        for i in range(count - 1, -1, -1):
                            try:
                                target_btn = buttons.nth(i)
                                btn_label = await target_btn.get_attribute('aria-label', timeout=500)
                                btn_text = await target_btn.inner_text(timeout=500) if not btn_label else ''
                                label = (btn_label or btn_text or '').lower()

                                print(f"  → [{name}] Button {i}: '{btn_label or btn_text or '(no text)'}'")

                                # Skip unwanted buttons (library glue)
                                if any(word in label for word in ['stop', 'cancel', 'prompt', 'input', 'question']):
                                    print(f"  ⚠ [{name}] Skipping: {btn_label or btn_text}")
                                    continue

                                # Found valid copy button - click it
                                await target_btn.click(force=True, timeout=2000)
                                copied = True
                                print(f"  ✓ [{name}] Copy button clicked")
                                break
                            except:
                                continue

                        if copied:
                            break
                except Exception as e:
                    continue

            if not copied:
                print(f"  ⚠ [{name}] No copy button found, will try text extraction")

            # === EXTRACTION CHAIN ===
            response_text, method, matches = None, None, []

            # Tier 1: <answer> tags - check both HTML (preserves formatting) and innerText, pick best
            if name not in _skip_answer_tag_wait:
                print(f"  → [{name}] Checking <answer> tags in innerText...")
                html_matches, html_ok = extract_response_by_answer_tags(raw_html, query=query)
                text_matches, text_ok = extract_response_by_answer_tags(inner_text, query=query)
                html_best = html_matches[-1] if html_ok and html_matches else None
                text_best = text_matches[-1] if text_ok and text_matches else None
                # Prefer HTML (preserves code indentation) unless innerText match is longer
                if html_best and text_best:
                    if len(text_best) > len(html_best):
                        response_text, method, matches = text_best, 'answer_tag', text_matches
                    else:
                        response_text, method, matches = html_best, 'answer_tag_html', html_matches
                elif html_best:
                    response_text, method, matches = html_best, 'answer_tag_html', html_matches
                elif text_best:
                    response_text, method, matches = text_best, 'answer_tag', text_matches
                if response_text:
                    tag_info = f"{len(matches)} match(es), using last" if len(matches) > 1 else "1 match"
                    print(f"  ✓ [{name}] Answer tag ({method.split('_')[-1] if '_' in method else 'text'}): {len(response_text)} chars ({tag_info})")

            # Tier 2: Copy button + clipboard (parse answer tags from clipboard too)
            if not response_text and copied:
                try:
                    clip = await page.evaluate("async()=>{try{return await navigator.clipboard.readText()}catch{return null}}")
                    if clip:
                        # Reject if clipboard is just the prompt text
                        if clip.strip().startswith(query[:40]):
                            print(f"  ⚠ [{name}] Clipboard has prompt text, skipping")
                        else:
                            # Check if clipboard contains answer tags (e.g. "<answer>4</answer>")
                            clip_matches, clip_ok = extract_response_by_answer_tags(clip, query=query)
                            if clip_ok and clip_matches:
                                response_text = clip_matches[-1]
                                method = 'copy_tag'
                                print(f"  ✓ [{name}] Copy + tag parse: {len(response_text)} chars")
                            else:
                                response_text = clip
                                method = 'copy'
                except: pass

            # Tier 3: Platform-specific DOM selectors
            if not response_text:
                try:
                    response_text = await page.evaluate("""(name) => {
                        const m = {
                            Claude:['.font-claude-message','[class*="claude-message"]','.prose'],
                            ChatGPT:['div[data-message-author-role="assistant"] .markdown','div[data-message-author-role="assistant"]'],
                            Gemini:['message-content .markdown','message-content','.model-response-text'],
                            DeepSeek:['.ds-markdown','.markdown-body','[class*="markdown"]'],
                            Grok:['.message-text','[class*="response-message"]'],
                            Perplexity:['.prose','[class*="answer"]','.markdown'],
                            Qwen:['.message-content','[class*="answer"]'],
                            AIStudio:['.response-content','.markdown-body'],
                            Ernie:['#answer_text_id','.md-stream-desktop','.custom-html'],
                            Kimi:['.segment-assistant .markdown','.segment-content-box','.segment-assistant .paragraph'],
                            Zai:['.markdown-body','[class*="markdown"]','.prose','[class*="message"]']
                        };
                        const sels = (m[name]||[]).concat(['[class*="response"]','article','main']);
                        for(const s of sels){const els=document.querySelectorAll(s);if(els.length>0){const t=els[els.length-1].innerText?.trim();if(t&&t.length>1)return t}}
                        return null}""", name)
                    if response_text:
                        method = 'dom'
                except: pass

            # Tier 4: Claude CLI extraction from innerText (library glue)
            if not response_text:
                print(f"  → [{name}] Claude CLI extraction from innerText...")
                claude_text, claude_ok = await asyncio.to_thread(
                    extract_response_via_claude, inner_text, name, query
                )
                if claude_ok and claude_text:
                    response_text = claude_text
                    method = 'claude_extract'
                    print(f"  ✓ [{name}] Claude extracted {len(claude_text)} chars")
                else:
                    print(f"  ⚠ [{name}] Claude extraction: no response found")

            # Tier 5: Raw HTML fallback
            if not response_text:
                response_text = raw_html
                method = 'html_fallback'
                print(f"  ⚠ [{name}] Copying didn't work - fell back to raw HTML ({len(raw_html)} chars)")

            # Save response text to data dir (library glue)
            response_path = _save('response.txt', response_text)

            # Save all answer tag matches if any (library glue)
            if matches:
                for i, m in enumerate(matches):
                    _save(f'answer_tag_{i}.txt', m)

            # Save metadata (library glue)
            _save('meta.txt', f"URL: {current_url}\nMETHOD: {method}\nLENGTH: {len(response_text)}\nCOPIED: {copied}\nRESPONSE_READY: {response_ready}\nANSWER_TAGS: {len(matches)}\n")

            # Also save to /tmp for backwards compat
            try:
                open(f'/tmp/{name.lower()}_response.txt','w').write(response_text)
                open(f'/tmp/{name.lower()}_response.html','w').write(raw_html)
            except: pass

            return (name, response_text, _platform_dir, method)

        except Exception as e:
            print(f"  ✗ [{name}] Error: {str(e)[:60]}" + (f", retrying ({_r-1} left)" if _r > 1 else ""))
            # Save error to data dir
            try: _save('meta.txt', f"URL: {page.url if page else 'unknown'}\nSTATUS: error\nERROR: {str(e)}\n")
            except: pass
            return await send_and_get_response(name, page, _r-1) if _r > 1 else (name, None, None, None)

    print("\n" + "═" * 50)
    print("SENDING QUERIES AND WAITING FOR RESPONSES")
    print("═" * 50)

    tasks = [send_and_get_response(n, p) for n, p in pages]
    responses = await asyncio.gather(*tasks)

    # Print results with data paths (library glue)
    print("\n" + "═" * 50 + "\nRESULTS SUMMARY\n" + "═" * 50)
    print(f"Data dir: {_data_dir}\n")
    for name, response, data_path, method in responses:
        if response and method:
            preview = (response[:60] + '...').replace('\n',' ') if len(response) > 60 else response.replace('\n',' ')
            if method == 'html_fallback':
                print(f"⚠ {name}: Copying didn't work - raw HTML saved ({len(response)} chars)")
            else:
                print(f"✓ {name} [{method}]: {preview}")
            print(f"  → {data_path}/")
        else:
            print(f"✗ {name}: Failed")

    print(f"\n  All data logged to: {_data_dir}")
    print("═" * 50 + "\nPress Ctrl+C to exit...\n" + "═" * 50)

    try:
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        print("\n✓ Closing...")
        await browser.close()
        await playwright.stop()
        close_browser()

def multi_ai_workflow(query=None, only=None, files=None):
    """Wrapper for async multi-AI (library glue)"""
    return asyncio.run(multi_ai_async(query, only=only, files=files))

LLM_LOGIN_URLS = {'claude':'https://claude.ai/','chatgpt':'https://chatgpt.com/','grok':'https://grok.com/','deepseek':'https://chat.deepseek.com/','gemini':'https://gemini.google.com/app','gemini1':'https://gemini.google.com/u/1/app','perplexity':'https://www.perplexity.ai/','qwen':'https://chat.qwen.ai/','aistudio':'https://aistudio.google.com/','ernie':'https://ernie.baidu.com/','kimi':'https://www.kimi.com/','zai':'https://chat.z.ai/'}

def status(only=None):
    """Login status across LLM_LOGIN_URLS. Dumps screenshot+HTML when logged out."""
    launch_browser_with_positioning();pg=get_page();r={}
    targets={k:v for k,v in LLM_LOGIN_URLS.items() if not only or k in only}
    for nm,url in targets.items():
        try:
            pg.goto(url,wait_until='domcontentloaded',timeout=15000);time.sleep(2)
            body=(pg.evaluate('document.body.innerText') or '')[:3000]
            lo=any(t in body for t in('Sign in','Log in','Sign up','Get started','Continue with'))
            r[nm]='out' if lo else 'in'
            print(f"{'x' if lo else '+'} {nm}: {r[nm]}")
            if lo:
                d=f'/tmp/login_{nm}';os.makedirs(d,exist_ok=True)
                pg.screenshot(path=f'{d}/shot.png')
                open(f'{d}/body.html','w').write(pg.evaluate('document.documentElement.outerHTML') or '')
                print(f"  → {d}/")
        except Exception as e:r[nm]='err';print(f"? {nm}: {str(e)[:60]}")
    return r

def _click_text(pg, regex):
    """Click first button/a/div whose visible text matches case-insensitive regex. Returns True if clicked."""
    js = f'(()=>{{const r=new RegExp({regex!r},"i");const e=Array.from(document.querySelectorAll("button,a,div,span")).find(x=>x.innerText&&r.test(x.innerText.trim().slice(0,80)));if(e){{e.click();return true}}return false}})()'
    return bool(pg.evaluate(js))

def signin(only=None):
    """Light auto sign-in attempt on logged-out platforms. Two clicks max (Sign in → Continue with Google).
    Recommends manual sign-in for failures."""
    out=[k for k,v in status(only=only).items() if v=='out']
    if not out: print("\n✓ all logged in"); return
    pg=get_page();failed=[]
    print(f"\n→ attempting auto sign-in for: {', '.join(out)}")
    for nm in out:
        try:
            pg.goto(LLM_LOGIN_URLS[nm],wait_until='domcontentloaded',timeout=15000);time.sleep(3)
            if _click_text(pg,r'^(sign in|log in|get started)$'): time.sleep(3)
            if _click_text(pg,r'continue with google|sign in with google'): time.sleep(5)
            body=(pg.evaluate('document.body.innerText') or '')[:3000]
            ok=not any(t in body for t in ('Sign in','Log in','Sign up','Get started','Continue with'))
            print(f"{'+' if ok else 'x'} {nm}: {'in' if ok else 'still out'}")
            if not ok: failed.append(nm)
        except Exception as e: print(f"? {nm}: {str(e)[:60]}"); failed.append(nm)
    if failed:
        print(f"\n→ recommend manual sign-in for: {', '.join(failed)}")
        print(f"  python3 lib/agui.py log   # browser stays open, sign in to each, Ctrl+C to save")
    else: print("\n✓ all logged in")


_DEEPTHINK_CACHE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'adata', 'local', 'agui_deepthink_acct.txt')

async def _probe_gemini_deepthink(ctx, n):
    """Open /u/{n}/app, click Tools (aria-label), check body for 'Deep think' (case-insens)."""
    p = await ctx.new_page()
    try:
        await p.goto(f'https://gemini.google.com/u/{n}/app', wait_until='domcontentloaded', timeout=30000)
        await p.wait_for_selector('button[aria-label="Tools"]', timeout=10000)
        await asyncio.sleep(2)  # Gemini SPA needs time to fully hydrate
        await p.evaluate('document.querySelector(\'button[aria-label="Tools"]\').click()')
        await asyncio.sleep(1)
        if await p.evaluate("/deep think/i.test(document.body.innerText)"):
            print(f"  ✓ /u/{n}/app has Deep Think")
            return p
        print(f"  - /u/{n}/app: no Deep Think")
    except Exception as e: print(f"  ? /u/{n}/app: {str(e)[:60]}")
    try: await p.close()
    except: pass
    return None

async def gemini_deepthink_async(prompt):
    """Probe cached then /u/0..2 for Deep Think, activate, submit. Cache the winner."""
    try: cached = open(_DEEPTHINK_CACHE).read().strip()
    except: cached = None
    _launch_chrome_positioned()
    pw = await async_playwright().start()
    ctx = (await pw.chromium.connect_over_cdp('http://localhost:9222')).contexts[0]
    page = None
    order = ([cached] if cached else []) + [c for c in ('0','1','2') if c != cached]
    for n in order:
        page = await _probe_gemini_deepthink(ctx, n)
        if page:
            if n != cached:
                os.makedirs(os.path.dirname(_DEEPTHINK_CACHE), exist_ok=True)
                open(_DEEPTHINK_CACHE, 'w').write(n)
                print(f"  → cached account /u/{n}")
            break
    if not page: print("x no Gemini account has Deep Think available"); return
    # Click Deep Think menu item by case-insensitive text match
    await page.evaluate("(()=>{const e=Array.from(document.querySelectorAll('*')).find(x=>x.children.length===0 && /^deep think$/i.test((x.innerText||'').trim()));if(e)e.click()})()")
    await asyncio.sleep(1)
    elem = page.locator('div[role="textbox"]').last
    await elem.click(timeout=2000); await elem.fill(prompt); await page.keyboard.press('Enter')
    print(f"  ✓ submitted, conversation: {page.url}")
    print(f"  → revisit URL when done (Deep Think takes minutes)")
    # Deep Think takes minutes — keep chrome alive after script exit so user can revisit
    global _profile_dir, _chrome_process
    _profile_dir = None; _chrome_process = None

def gemini_deepthink(prompt): asyncio.run(gemini_deepthink_async(prompt))

_AGUI_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'adata', 'local', 'agui')

def _extract_via_ollama(text, model='gemma4:latest'):
    """Ask local ollama to pull the headline answer from a DR report. Returns string or None."""
    import urllib.request, json as _j
    prompt = f"Extract the single headline answer to the question asked at the top of this report. Reply with ONLY the answer (a number, name, or short phrase), nothing else, no explanation.\n\n{text[:8000]}"
    try:
        req = urllib.request.Request('http://localhost:11434/api/generate',
            data=_j.dumps({'model':model,'prompt':prompt,'stream':False,'options':{'temperature':0}}).encode(),
            headers={'content-type':'application/json'})
        return _j.loads(urllib.request.urlopen(req, timeout=60).read()).get('response','').strip()
    except Exception as e:
        print(f"  ollama extract failed: {e}"); return None

def _latest_gemini_url():
    """Find the most recent gemini conversation URL saved by deep_research_async."""
    if not os.path.isdir(_AGUI_DIR): return None
    for ts in sorted(os.listdir(_AGUI_DIR), reverse=True):
        f = os.path.join(_AGUI_DIR, ts, 'gemini', 'url.txt')
        if os.path.exists(f):
            u = open(f).read().strip()
            if u: return u
    return None

async def drfetch_async(url=None, watch=False):
    """Resume DR conversation. Gemini: longest .markdown element. ChatGPT: response is in a
    cross-origin sandbox iframe — extract via printToPDF + pdftotext. Claude: longest .markdown
    or .font-claude-message. watch=True polls every 15s until length stabilizes."""
    url = url or _latest_gemini_url()
    if not url: print("x no saved url; provide one or run 'deep' first"); return
    print(f"  url: {url}")
    is_chatgpt = 'chatgpt.com' in url
    if not _cdp_already_running(): _launch_chrome_positioned()
    pw = await async_playwright().start()
    ctx = (await pw.chromium.connect_over_cdp('http://localhost:9222')).contexts[0]
    page = await ctx.new_page()
    await page.goto(url, wait_until='domcontentloaded', timeout=30000)
    await asyncio.sleep(20 if is_chatgpt else 3)  # ChatGPT DR iframes need ~10-20s to hydrate
    async def _extract():
        if is_chatgpt:
            import base64, subprocess, tempfile
            # Expand the DR iframe + strip overflow on ancestors so the iframe's content
            # area is unconstrained, then printToPDF (CDP renders the cross-origin iframe
            # contents that DOM access can't reach). Tall paper to fit a long report.
            await page.evaluate("""(()=>{const fs=document.querySelectorAll('iframe[title*="deep-research"]');for(const f of fs){f.style.height='50000px';f.style.minHeight='50000px';f.style.maxHeight='none';let p=f.parentElement;for(let i=0;i<12&&p;i++){p.style.overflow='visible';p.style.maxHeight='none';p.style.height='auto';p=p.parentElement}}})()""")
            await asyncio.sleep(3)
            cdp = await page.context.new_cdp_session(page)
            r = await cdp.send('Page.printToPDF', {'printBackground': True, 'paperHeight': 400, 'paperWidth': 8.5})
            data = base64.b64decode(r.get('data', ''))
            with tempfile.NamedTemporaryFile(suffix='.pdf', delete=False) as f: f.write(data); pdfp = f.name
            try: txt = subprocess.check_output(['pdftotext', '-layout', pdfp, '-']).decode(errors='replace')
            finally: os.unlink(pdfp)
            return txt
        # Gemini / Claude: longest text in any plausible response container
        return await page.evaluate("(()=>{const sels=['.markdown','.standard-markdown','.font-claude-message','.prose','[data-test-render-count]'];let b='';for(const s of sels)for(const m of document.querySelectorAll(s)){const t=m.innerText||'';if(t.length>b.length)b=t}return b})()") or ''
    last_len = -1; rep = ''
    while True:
        rep = await _extract()
        cur = len(rep)
        print(f"  report: {cur} chars{'' if not watch else ' (watching)'}")
        if not watch: break
        if cur and cur == last_len: print('  stable — done'); break
        last_len = cur
        await asyncio.sleep(15)
    if rep:
        out = os.path.join(_AGUI_DIR, 'drfetch'); os.makedirs(out, exist_ok=True)
        ts = time.strftime('%Y%m%d_%H%M%S')
        f = os.path.join(out, ts+'.txt'); open(f,'w').write(rep)
        # Try to extract answer: <answer> tag, or first big number in executive summary
        import re as _re
        ans = None
        m = _re.search(r'<answer>([^<]+)</answer>', rep)
        if m: ans = m.group(1).strip()
        else:
            # First well-formed number with thousands separators (e.g. 14,192,184 or 1,234.56).
            # In DR exec summaries this is almost always the headline figure.
            m = _re.search(r'\b(\d{1,3}(?:,\d{3})+(?:\.\d+)?)\b', rep)
            if m: ans = m.group(1)
        if not ans:
            print("  regex didn't match — falling back to ollama gemma4...")
            raw = _extract_via_ollama(rep)
            if raw:
                m = _re.search(r'<answer>([^<]+)</answer>', raw)
                ans = (m.group(1) if m else raw).strip()
        if ans:
            af = os.path.join(out, ts+'.answer.txt'); open(af,'w').write(ans)
            print(f"  answer: {ans}")
            print(f"  saved: {f}, {af}")
        else:
            print(f"  saved: {f} (no answer pattern matched — read the report)")
    global _profile_dir, _chrome_process
    _profile_dir = None; _chrome_process = None
    await pw.stop()

def drfetch(url=None, watch=False): asyncio.run(drfetch_async(url, watch))

# ═══════════════════════════════════════════════════════════════════
# DEEP RESEARCH - Parallel launch, save URLs, exit
# ═══════════════════════════════════════════════════════════════════

async def _launch_deep_gemini(page, query):
    """Activate Gemini Deep Research, type query, return conversation URL"""
    await page.wait_for_selector('button[aria-label="Tools"]', timeout=10000)
    await asyncio.sleep(2)

    # Tools button has empty innerText, only aria-label; menu items use mixed-case "Deep research"
    await page.evaluate('document.querySelector(\'button[aria-label="Tools"]\').click()')
    await asyncio.sleep(1)
    await page.evaluate("(()=>{const e=Array.from(document.querySelectorAll('*')).find(x=>x.children.length===0 && /^deep research$/i.test((x.innerText||'').trim()));if(e)e.click()})()")
    print(f"  ✓ [Gemini] Deep Research mode active")
    await asyncio.sleep(1)

    # Type and submit
    elem = page.locator('div[role="textbox"]').last
    await elem.click(timeout=2000)
    await elem.fill(query)
    await page.keyboard.press('Enter')
    print(f"  ✓ [Gemini] Query submitted")

    # Wait for research plan, then confirm start
    await asyncio.sleep(8)
    try:
        btn = page.locator('button:has-text("Start research")')
        await btn.click(timeout=8000)
        print(f"  ✓ [Gemini] Start research clicked")
    except:
        try:
            elem2 = page.locator('div[role="textbox"]').last
            await elem2.click(timeout=2000)
            await elem2.fill("Start research")
            await page.keyboard.press('Enter')
            print(f"  ✓ [Gemini] Start research typed")
        except:
            print(f"  ⚠ [Gemini] Could not confirm start - check manually")

    await asyncio.sleep(3)
    return page.url


async def _launch_deep_chatgpt(page, query):
    """Activate ChatGPT Deep Research, type query, return conversation URL"""
    await page.wait_for_selector('#prompt-textarea, [contenteditable="true"]', timeout=10000)
    await asyncio.sleep(2)

    # Activate deep research mode
    try:
        await page.locator('button:has(svg):near([contenteditable])').first.click(timeout=2000)
        await page.wait_for_selector('text="Deep research"', timeout=2000)
        await page.locator('text="Deep research"').click(timeout=2000)
        print(f"  ✓ [ChatGPT] Deep Research mode active")
    except:
        try:
            box = await page.locator('[contenteditable="true"]').first.bounding_box()
            if box:
                await page.mouse.click(box['x'] - 25, box['y'] + box['height'] / 2)
            await page.wait_for_selector('text="Deep research"', timeout=2000)
            await page.locator('text="Deep research"').click(timeout=2000)
            print(f"  ✓ [ChatGPT] Deep Research mode active (fallback)")
        except:
            print(f"  ⚠ [ChatGPT] Could not activate mode - continuing with prompt")

    await asyncio.sleep(1)

    # Type query (ProseMirror needs keyboard events)
    elem = page.locator('#prompt-textarea, [contenteditable="true"]').last
    await elem.click(timeout=2000)
    await elem.press_sequentially(query, delay=5)
    await page.keyboard.press('Enter')
    print(f"  ✓ [ChatGPT] Query submitted")

    # Modern ChatGPT DR auto-runs after submit; if a "Start research" button appears,
    # click it once. Never type "Start research" as a chat msg (creates phantom 2nd prompt).
    await asyncio.sleep(8)
    try:
        await page.locator('button:has-text("Start research")').first.click(timeout=4000)
        print(f"  ✓ [ChatGPT] Start research clicked")
    except: pass  # auto-run path
    await asyncio.sleep(3)
    return page.url


async def _launch_deep_claude(page, query):
    """Activate REAL Claude Deep Research: + menu (Add files, connectors, and more) → Research."""
    await page.wait_for_selector('div[contenteditable="true"]', timeout=15000)
    await asyncio.sleep(2)
    # Open the + menu
    await page.evaluate("(()=>{const b=Array.from(document.querySelectorAll('button')).find(x=>(x.ariaLabel||'').toLowerCase().includes('add files'));if(b)b.click()})()")
    await asyncio.sleep(1)
    # Click the Research menuitem (case-insensitive exact match to avoid 'Web search'/'Use style')
    clicked = await page.evaluate("(()=>{const e=Array.from(document.querySelectorAll('[role=menuitem],button,div')).find(x=>x.children.length===0&&/^research$/i.test((x.innerText||'').trim()));if(e){e.click();return true}return false})()")
    if clicked: print(f"  ✓ [Claude] Deep Research mode activated (+ menu → Research)")
    else: print(f"  ⚠ [Claude] Research item not found in + menu")
    await asyncio.sleep(1)
    elem = page.locator('div[contenteditable="true"]').last
    await elem.click(timeout=3000)
    await elem.press_sequentially(query, delay=5)
    await page.keyboard.press('Enter')
    print(f"  ✓ [Claude] Query submitted")
    await asyncio.sleep(5)
    return page.url


async def _launch_deep_perplexity(page, query):
    """Activate REAL Perplexity Deep Research: + menu (Add files or tools) → Deep research item.
    Uses CDP Input.dispatchMouseEvent — Playwright .click() and JS .click() both miss the
    React handler on Perplexity's button. Pro tier required for Deep research item to appear."""
    await page.wait_for_selector('button[aria-label="Add files or tools"]', timeout=15000)
    await asyncio.sleep(2)
    # Dismiss cookie modal if present
    await page.evaluate("(()=>{const b=Array.from(document.querySelectorAll('button')).find(x=>(x.innerText||'').trim()==='Got it');if(b)b.click()})()")
    await asyncio.sleep(0.5)
    cdp = await page.context.new_cdp_session(page)
    async def _cdp_click(sel):
        box = await page.evaluate(f"(()=>{{const e=document.querySelector({sel!r});if(!e)return null;const r=e.getBoundingClientRect();return{{x:r.x+r.width/2,y:r.y+r.height/2}}}})()")
        if not box: return False
        await cdp.send('Input.dispatchMouseEvent',{'type':'mouseMoved','x':box['x'],'y':box['y']})
        await cdp.send('Input.dispatchMouseEvent',{'type':'mousePressed','x':box['x'],'y':box['y'],'button':'left','clickCount':1})
        await cdp.send('Input.dispatchMouseEvent',{'type':'mouseReleased','x':box['x'],'y':box['y'],'button':'left','clickCount':1})
        return True
    # 1) Open + menu via CDP click
    await _cdp_click('button[aria-label="Add files or tools"]')
    await asyncio.sleep(1.5)
    # 2) Click "Deep research" menu item by text match
    found = await page.evaluate("(()=>{const e=Array.from(document.querySelectorAll('[role=menuitem],div,button,span')).find(x=>x.children.length===0&&/^deep research$/i.test((x.innerText||'').trim()));if(e){const r=e.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2}}return null})()")
    if found:
        await cdp.send('Input.dispatchMouseEvent',{'type':'mousePressed','x':found['x'],'y':found['y'],'button':'left','clickCount':1})
        await cdp.send('Input.dispatchMouseEvent',{'type':'mouseReleased','x':found['x'],'y':found['y'],'button':'left','clickCount':1})
        print(f"  ✓ [Perplexity] Deep Research mode activated")
    else:
        print(f"  ⚠ [Perplexity] Deep research item not found (Pro tier required?)")
    await asyncio.sleep(1)
    # 3) Type prompt + submit via Playwright keyboard
    elem = page.locator('textarea, div[contenteditable="true"]').first
    await elem.click(timeout=3000)
    await asyncio.sleep(0.3)
    await page.keyboard.type(query, delay=10)
    await asyncio.sleep(0.5)
    await page.keyboard.press('Enter')
    print(f"  ✓ [Perplexity] Query submitted")
    await asyncio.sleep(5)
    return page.url


_deep_launchers = {
    'ChatGPT': _launch_deep_chatgpt,
    'Gemini': _launch_deep_gemini,
    'Claude': _launch_deep_claude,
    'Perplexity': _launch_deep_perplexity,
}


async def deep_research_async(query, only=None):
    """Parallel deep research - launch, save URLs, exit (library glue - async)"""
    from datetime import datetime as _dt

    print("\n" + "=" * 50)
    print("Deep Research (Parallel Launch)")
    print("=" * 50)
    print(f"\n  → Query: {query[:100]}{'...' if len(query)>100 else ''}\n")

    # Platform list (extensible)
    platforms = [
        ('ChatGPT', 'https://chatgpt.com/'),
        ('Gemini', 'https://gemini.google.com/app'),
        ('Claude', 'https://claude.ai/'),
        ('Perplexity', 'https://www.perplexity.ai/'),
    ]
    if only:
        only_list = [o.strip().lower() for o in (only.split(',') if isinstance(only, str) else only)]
        platforms = [(n, u) for n, u in platforms if n.lower() in only_list]
    if not platforms:
        print("  ✗ No matching platforms. Available: ChatGPT, Gemini, Claude, Perplexity")
        return

    # Launch Chrome + async Playwright
    _launch_chrome_positioned()
    playwright = await async_playwright().start()
    browser = await playwright.chromium.connect_over_cdp('http://localhost:9222')
    context = browser.contexts[0]
    existing = [p for p in context.pages if not p.url.startswith(('chrome://', 'chrome-extension://'))]
    first_page = existing[0] if existing else await context.new_page()

    # Data directory
    _run_ts = _dt.now().strftime('%Y%m%d_%H%M%S')
    _data_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'adata', 'local', 'agui', _run_ts)
    os.makedirs(_data_dir, exist_ok=True)

    async def open_platform(name, url, page=None):
        try:
            page = page or await context.new_page()
            await page.goto(url, wait_until='domcontentloaded', timeout=30000)
            print(f"  ✓ [{name}] Loaded")
            return (name, page, True)
        except Exception as e:
            print(f"  ✗ [{name}] {str(e)[:50]}")
            return (name, page, False)

    tasks = [open_platform(n, u, first_page if i == 0 else None)
             for i, (n, u) in enumerate(platforms)]
    tab_results = await asyncio.gather(*tasks)
    pages = [(n, p) for n, p, ok in tab_results if ok and p]

    # Activate deep research per platform
    async def activate(name, page):
        _platform_dir = os.path.join(_data_dir, name.lower())
        os.makedirs(_platform_dir, exist_ok=True)
        def _save(filename, content):
            p = os.path.join(_platform_dir, filename)
            with open(p, 'w') as f:
                f.write(content if content else '')
            return p

        launcher = _deep_launchers.get(name)
        if not launcher:
            print(f"  ✗ [{name}] No deep research launcher")
            _save('meta.txt', f"PLATFORM: {name}\nSTATUS: no_launcher\n")
            return (name, None, _platform_dir)

        try:
            url = await launcher(page, query)
            await page.screenshot(path=os.path.join(_platform_dir, 'launch.png'))
            _save('url.txt', url or page.url)
            _save('meta.txt', (
                f"PLATFORM: {name}\n"
                f"URL: {url or page.url}\n"
                f"QUERY: {query}\n"
                f"TIMESTAMP: {_dt.now().isoformat()}\n"
                f"STATUS: launched\n"
            ))
            return (name, url or page.url, _platform_dir)
        except Exception as e:
            print(f"  ✗ [{name}] Launch failed: {str(e)[:60]}")
            try:
                await page.screenshot(path=os.path.join(_platform_dir, 'launch.png'))
            except: pass
            _save('meta.txt', f"PLATFORM: {name}\nSTATUS: error\nERROR: {str(e)}\n")
            return (name, None, _platform_dir)

    launch_tasks = [activate(n, p) for n, p in pages]
    results = await asyncio.gather(*launch_tasks)

    # Summary
    print("\n" + "=" * 50)
    print("DEEP RESEARCH LAUNCHED")
    print("=" * 50)
    for name, url, data_path in results:
        if url:
            print(f"  ✓ {name}: {url}")
        else:
            print(f"  ✗ {name}: Failed")
    print(f"\n  Data: {_data_dir}")
    print(f"  Revisit URLs above when research completes (5-15 min).")
    print("=" * 50)
    # Deep Research takes minutes — keep chrome alive after exit so user can revisit URLs
    global _profile_dir, _chrome_process
    _profile_dir = None; _chrome_process = None
    await browser.close()
    await playwright.stop()


def deep_research_workflow(query=None, only=None):
    """Wrapper for async deep research (library glue)"""
    if not query:
        query = input("\n➤ Deep research prompt: ").strip() or "Explain this topic in depth"
    return asyncio.run(deep_research_async(query, only=only))


def chatgpt_deep_research_workflow(prompt=None, _retry=0):
    """ChatGPT Deep Research - event-driven, fast (library glue)"""
    print(f"═══ Deep Research {'(retry '+str(_retry)+')' if _retry else ''} ═══\n")
    if not prompt: prompt = "What are the latest developments in quantum computing?"
    print(f"  → {prompt[:80]}{'...' if len(prompt)>80 else ''}\n")

    launch_browser_with_positioning()
    # Open all pages at start (parallel loading like multi-LLM)
    gem = _browser_context.new_page(); gem.goto('https://gemini.google.com/app', wait_until='domcontentloaded')
    claude = _browser_context.new_page(); claude.goto('https://claude.ai/', wait_until='domcontentloaded')
    if not navigate_to_robust('https://chatgpt.com', max_retries=2)[0]: print("  ✗ Failed"); return
    page = get_page()

    # Claude - click plus button to get menu
    print("  → Opening Claude menu...")
    try:
        claude.wait_for_selector('div[contenteditable="true"]', timeout=10000)
        claude.locator('button:has(svg[class*="plus"]), button[aria-label*="Add"], button:has-text("+")').first.click(timeout=3000)
        print("  ✓ Claude menu opened")
    except: print("  ⚠ Claude plus button not found - continuing")

    # Activate Gemini Deep Research
    print("  → Activating Gemini Deep Research...")
    try:
        gem.wait_for_selector('div[role="textbox"]', timeout=10000)
        gem.locator('text="Tools"').click(timeout=3000)
        gem.wait_for_selector('text="Deep Research"', timeout=3000)
        gem.locator('text="Deep Research"').click(timeout=2000)
        print("  ✓ Gemini Deep Research mode active")
        elem = gem.locator('div[role="textbox"]').last
        elem.click(timeout=2000); elem.fill(prompt); gem.keyboard.press('Enter')
        print("  ✓ Gemini prompt sent")
    except: print("  ⚠ Gemini Deep Research not found - continuing")

    # Activate ChatGPT Deep Research
    try: page.wait_for_selector('#prompt-textarea, [contenteditable="true"]', timeout=10000)
    except: print("  ✗ Page not ready"); return
    inp = page.locator('#prompt-textarea, [contenteditable="true"]').first
    inp.click(timeout=2000); page.keyboard.press('Control+a'); page.keyboard.press('Backspace')
    print("  → Activating ChatGPT Deep Research...")
    try:
        page.locator('button:has(svg):near([contenteditable])').first.click(timeout=2000)
        page.wait_for_selector('text="Deep research"', timeout=2000)
        page.locator('text="Deep research"').click(timeout=2000)
        print("  ✓ ChatGPT Deep Research mode active")
    except:
        try:
            box = page.locator('[contenteditable="true"]').first.bounding_box()
            if box: page.mouse.click(box['x'] - 25, box['y'] + box['height']/2)
            page.wait_for_selector('text="Deep research"', timeout=2000)
            page.locator('text="Deep research"').click(timeout=2000)
            print("  ✓ ChatGPT Deep Research mode active (fallback)")
        except: print("  ⚠ Could not verify - continuing")

    page.screenshot(path='/tmp/after_more_click.png')
    copy_before = page.locator('button[aria-label*="Copy"]').count()

    # Send prompt - pressSequentially for ProseMirror (fires real keyboard events)
    print("  → Sending prompt...")
    try:
        elem = page.locator('#prompt-textarea, [contenteditable="true"]').last
        elem.click(timeout=2000); elem.press_sequentially(prompt, delay=5); page.keyboard.press('Enter')
        print("  ✓ Prompt sent")
    except: print("  ⚠ Prompt send failed")

    # EVENT: Wait for copy button (clarifying questions ready)
    print("  → Waiting for clarifying questions...")
    try:
        page.wait_for_function(f"""(before) => {{
            const stops = document.querySelectorAll('[aria-label*="Stop"], button:has-text("Stop")');
            const copies = document.querySelectorAll('button[aria-label*="Copy"]');
            return stops.length === 0 && copies.length > before;
        }}""", arg=copy_before, timeout=120000, polling=300)
        print("  ✓ Clarifying questions received")
    except: print("  ⚠ Timeout - continuing")

    # TODO: 'start' entry doesn't submit properly - types but Enter doesn't fire. User can press Enter manually for now.
    print("  → Typing 'start' (press Enter manually if needed)...")
    copy_before_research = page.locator('button[aria-label*="Copy"]').count()
    try:
        elem = page.locator('#prompt-textarea, [contenteditable="true"]').last
        elem.click(timeout=2000); elem.press_sequentially('start', delay=20); page.keyboard.press('Enter')
    except: pass

    # EVENT: Wait for deep research completion (new copy button appears)
    print("  → Deep Research running (2-5 min)...")
    try:
        page.wait_for_function(f"""(before) => {{
            const stops = document.querySelectorAll('[aria-label*="Stop"], button:has-text("Stop")');
            const copies = document.querySelectorAll('button[aria-label*="Copy"]');
            const busy = document.body.innerText.includes('Stop generating') || document.body.innerText.includes('Searching');
            return !busy && stops.length === 0 && copies.length > before;
        }}""", arg=copy_before_research, timeout=300000, polling=1000)
        print("  ✓ Deep Research complete")
    except: print("  ⚠ Timeout - may still be running")

    # Copy response
    try:
        btns = page.locator('button[aria-label*="Copy"]')
        if btns.count() > 0:
            btns.last.scroll_into_view_if_needed(); btns.last.click(force=True)
            txt = page.evaluate("async()=>{try{return await navigator.clipboard.readText()}catch{return ''}}")
            if txt: print(f"  ✓ Copied {len(txt)} chars: {txt[:100]}...")
    except: pass

    page.screenshot(path='/tmp/chatgpt_deep_research.png')
    print("\n✓ Done. Ctrl+C to exit.")
    try:
        while True: time.sleep(1)
    except KeyboardInterrupt: print("\n✓ Closing..."); close_browser()

def test_navigation_workflow():
    """Test robust navigation with search engine fallback (library glue)"""
    print("═" * 50)
    print("Navigation Test - Robust URL Opening")
    print("═" * 50 + "\n")

    print("  → Launching browser with positioning...")
    launch_browser_with_positioning()

    # Test 1: Direct URL with robust navigation
    print("\n→ Test 1: Direct URL (Google)")
    success, final_url = navigate_to_robust('https://google.com', max_retries=3)
    print(f"  Result: {'✓ SUCCESS' if success else '✗ FAILED'} - {final_url}")
    take_screenshot('/tmp/test_google.png')

    # Test 2: Search engine fallback (Bing first)
    print("\n→ Test 2: Search Engine Fallback (Bing priority)")
    success, final_url, engine = navigate_with_search_fallback(query='python programming')
    print(f"  Result: {'✓ SUCCESS' if success else '✗ FAILED'} - Engine: {engine} - {final_url}")
    take_screenshot('/tmp/test_search.png')

    # Test 3: Direct URL that might fail (to test retry)
    print("\n→ Test 3: Test URL Detection")
    current_url = get_current_url()
    is_blank = is_page_blank()
    print(f"  Current URL: {current_url}")
    print(f"  Is Blank: {is_blank}")

    print("\n" + "═" * 50)
    print("✓ Navigation tests complete")
    print("Screenshots:")
    print("  - /tmp/test_google.png")
    print("  - /tmp/test_search.png")
    print("═" * 50)

    print("\nPress Ctrl+C to exit...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n✓ Closing...")
        close_browser()

# ═══════════════════════════════════════════════════════════
# WEB MONITOR - Track rankings in SQLite (library glue)
# ═══════════════════════════════════════════════════════════

def web_monitor_workflow(source='all'):
    """Web monitor - track rankings in SQLite (library glue)"""
    import sqlite3
    from datetime import datetime

    DB_PATH = os.path.expanduser('~/.waylandauto/data/web_monitor.db')
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.execute('PRAGMA journal_mode=WAL')
    conn.execute('CREATE TABLE IF NOT EXISTS monitor (source TEXT, rank INT, name TEXT, score TEXT, ts TEXT)')
    conn.execute('CREATE TABLE IF NOT EXISTS ack (source TEXT, change TEXT, ts TEXT)')

    last_ts = conn.execute('SELECT MAX(ts) FROM monitor').fetchone()[0]
    now = datetime.now()

    print("═" * 50)
    print(f"Web Monitor - {source}")
    print(f"  Now: {now.strftime('%Y-%m-%d %H:%M')}")
    print(f"  Last: {last_ts[:16].replace('T',' ') if last_ts else 'never'}")
    print("═" * 50)

    print("  → Launching browser...")
    launch_browser_with_positioning()

    sources = ['similarweb', 'arena', 'bloomberg'] if source == 'all' else [source]
    ts = datetime.now().isoformat()
    report = {}  # {source: {'changes': [], 'top': [], 'pending': []}}

    for src in sources:
        prev = dict(conn.execute(
            'SELECT name, rank FROM monitor WHERE source=? AND ts=(SELECT MAX(ts) FROM monitor WHERE source=?)', (src, src)
        ).fetchall())

        if src == 'similarweb':
            print(f"\n→ SimilarWeb Top Sites")
            success, _ = navigate_to_robust('https://www.similarweb.com/top-websites/', max_retries=3)
            if not success:
                print("  ✗ Navigation failed")
                continue
            rankings = []
            for attempt in range(5):
                try:
                    get_page().wait_for_selector('table tbody tr', timeout=3000)
                except:
                    pass
                rankings = get_page().evaluate('''() => {
                    const rows = [...document.querySelectorAll('table tbody tr')];
                    return rows.slice(0, 50).map((row, i) => {
                        const cells = row.querySelectorAll('td');
                        const link = row.querySelector('a[href*="/website/"]');
                        return {
                            rank: parseInt(cells[0]?.textContent) || i + 1,
                            name: link?.textContent?.trim() || cells[1]?.textContent?.trim() || '',
                            score: cells[2]?.textContent?.trim() || ''
                        };
                    }).filter(r => r.name && r.name.includes('.'));
                }''')
                if rankings:
                    break
            changes, pending = [], []
            for r in rankings:
                if r['name'] in prev and prev[r['name']] != r['rank']:
                    changes.append(f"  {r['name']}: #{prev[r['name']]} → #{r['rank']}")
                pending.append((src, r['rank'], r['name'], r['score'], ts))
            print(f"  ✓ {len(rankings)} entries")
            report[src] = {'changes': changes, 'top': rankings[:10], 'pending': pending}

        elif src == 'arena':
            print(f"\n→ LMArena Leaderboard")
            for board, url in [('arena-text', 'https://lmarena.ai/leaderboard/text'), ('arena-webdev', 'https://lmarena.ai/leaderboard/webdev')]:
                print(f"  → {board}...")
                success, _ = navigate_to_robust(url, max_retries=3)
                if not success:
                    continue
                rankings = []
                for attempt in range(5):
                    try:
                        get_page().wait_for_selector('table tbody tr, [class*="TableRow"]', timeout=3000)
                    except:
                        pass
                    rankings = get_page().evaluate('''() => {
                        let rows = [...document.querySelectorAll('table tbody tr')];
                        return rows.slice(0, 30).map((row, i) => {
                            const cells = row.querySelectorAll('td');
                            return {
                                rank: parseInt(cells[0]?.textContent) || i + 1,
                                name: cells[2]?.textContent?.trim() || '',
                                score: cells[3]?.textContent?.trim() || ''
                            };
                        }).filter(r => r.name && r.name.length > 3);
                    }''')
                    if rankings:
                        break
                changes, pending = [], []
                board_prev = dict(conn.execute(
                    'SELECT name, rank FROM monitor WHERE source=? AND ts=(SELECT MAX(ts) FROM monitor WHERE source=?)', (board, board)
                ).fetchall())
                for r in rankings:
                    if r['name'] in board_prev and board_prev[r['name']] != r['rank']:
                        changes.append(f"  {r['name']}: #{board_prev[r['name']]} → #{r['rank']}")
                    pending.append((board, r['rank'], r['name'], r['score'], ts))
                print(f"  ✓ {len(rankings)} models")
                report[board] = {'changes': changes, 'top': rankings[:10], 'pending': pending}

        elif src == 'bloomberg':
            print(f"\n→ Bloomberg Billionaires")
            success, _ = navigate_to_robust('https://www.bloomberg.com/billionaires/', max_retries=3)
            if not success:
                print("  ✗ Navigation failed")
                continue
            try:
                get_page().wait_for_selector('table tr, [class*="table-row"]', timeout=5000)
            except:
                pass
            # Scroll to load lazy content (library glue)
            print("  → Scrolling to load all...", end='', flush=True)
            last_pos = -1
            while True:
                pos = get_page().evaluate('window.scrollY')
                if pos == last_pos:
                    break
                last_pos = pos
                get_page().keyboard.press('PageDown')
                time.sleep(0.1)
            print(" done")
            rankings = get_page().evaluate(r'''() => {
                let rows = [...document.querySelectorAll('table tbody tr, [class*="table-row"]')];
                return rows.map((row, i) => {
                    const cells = row.querySelectorAll('td, [class*="cell"]');
                    const text = row.textContent || '';
                    const rankMatch = text.match(/^(\d+)/);
                    const worthMatch = text.match(/\$[\d.,]+\s*[BMT]/i);
                    return {
                        rank: rankMatch ? parseInt(rankMatch[1]) : i,
                        name: cells[1]?.textContent?.trim() || row.querySelector('a')?.textContent?.trim() || '',
                        score: worthMatch ? worthMatch[0] : ''
                    };
                }).filter(r => r.name && r.rank > 0 && !r.name.match(/^Name$/i));
            }''')
            changes, pending = [], []
            for r in rankings:
                if r['name'] in prev and prev[r['name']] != r['rank']:
                    changes.append(f"  {r['name']}: #{prev[r['name']]} → #{r['rank']}")
                pending.append((src, r['rank'], r['name'], r['score'], ts))
            print(f"  ✓ {len(rankings)} billionaires")
            report[src] = {'changes': changes, 'top': rankings[:10], 'pending': pending}

    # Consolidated report
    print("\n" + "═" * 50)
    print("REPORT")
    print("═" * 50)
    all_changes = []
    for src, data in report.items():
        if data['changes']:
            all_changes.append(f"\n{src}:")
            all_changes.extend(data['changes'][:10])

    if all_changes:
        print('\n'.join(all_changes))
        print("\n" + "─" * 50)
        ack = input("Acknowledge changes? (y/n): ").strip().lower()
        if ack in ('y', 'yes'):
            for src, data in report.items():
                conn.executemany('INSERT INTO monitor VALUES (?,?,?,?,?)', data['pending'])
                for c in data['changes']:
                    conn.execute('INSERT INTO ack VALUES (?,?,?)', (src, c.strip(), ts))
            conn.commit()
            print("✓ Changes acknowledged and saved")
        else:
            print("✗ Changes not saved")
    else:
        print("✓ No changes across all sources")
        for src, data in report.items():
            conn.executemany('INSERT INTO monitor VALUES (?,?,?,?,?)', data['pending'])
        conn.commit()

    # Cleanup before output (library glue)
    try:
        take_screenshot('/tmp/web_monitor.png')
        close_browser()
    except:
        pass
    conn.close()

    for src, data in report.items():
        print(f"\n{src} Top 10:")
        for r in data['top']:
            print(f"  #{r['rank']:2} {r['name']}")

    print(f"\n📁 DB: {DB_PATH}")

# ═══════════════════════════════════════════════════════════
# ENTRY POINT
# ═══════════════════════════════════════════════════════════

if __name__ == "__main__":
    import argparse

    # Show examples helper function (library glue pattern)
    def show_examples():
        examples = """
╔════════════════════════════════════════════════════════════════════╗
║                    MULTI-LLM PROMPT EXAMPLES                      ║
╠════════════════════════════════════════════════════════════════════╣
║                                                                    ║
║  BASIC PROMPT:                                                    ║
║    python3 agui.py --multi-ai --prompt "Your question"     ║
║                                                                    ║
║  EXAMPLES:                                                        ║
║    • python3 agui.py --multi-ai --prompt "Write a sort"    ║
║    • python3 agui.py --multi-ai --prompt "Explain Python"  ║
║    • python3 agui.py --multi-ai --prompt "Debug this code" ║
║                                                                    ║
║  WITH CONTENT:                                                    ║
║    • With PDF:   --multi-ai --prompt "Summarize" --pdf file.pdf   ║
║    • With paste: --multi-ai --prompt "Analyze" --paste            ║
║    • From pipe:  cat file.txt | python3 agui.py --paste    ║
║                                                                    ║
║  INTERACTIVE MODE:                                                ║
║    python3 agui.py --multi-ai                              ║
║    (Will prompt you to enter your question)                       ║
║                                                                    ║
╚════════════════════════════════════════════════════════════════════╝
"""
        print(examples)

    _cmds = {'go','bing','runs','side','hide','solo','art','loop','deep','all','test','rank','ask','say','put','doc','demo','pick','log','add','pw','canary','status','signin','deepthink','drfetch','watch'}
    sys.argv = [a if a.startswith('-') or a not in _cmds else f'--{a}' for a in sys.argv]
    parser = argparse.ArgumentParser(
        description='agui - GUI Automation with XWayland Tools',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Run with --multi-ai for interactive mode, or see examples above"
    )
    parser.add_argument('--go', '--google', action='store_true', help='Google verification')
    parser.add_argument('--side', '--right', action='store_true', help='Open on rightmost monitor')
    parser.add_argument('--hide', '--headless', action='store_true', help='Headless mode (no window)')
    parser.add_argument('--solo', '--isolated', action='store_true', help='Isolated profile (no cookies)')
    parser.add_argument('--deep', '--deep-research', action='store_true', help='Deep Research (ChatGPT + Gemini parallel)')
    parser.add_argument('--all', '--multi-ai', action='store_true', help='Multi-AI (8 LLMs parallel)')
    parser.add_argument('--test', '--test-nav', action='store_true', help='Test navigation')
    parser.add_argument('--rank', '--web-monitor', nargs='?', const='all', help='Track rankings')
    parser.add_argument('--ask', '--query', type=str, help='Prompt for multi-AI')
    parser.add_argument('--say', '--prompt', type=str, help='Prompt text for AI')
    parser.add_argument('--put', '--paste', action='store_true', help='Read from stdin')
    parser.add_argument('--doc', '--pdf', type=str, help='PDF file path')
    parser.add_argument('--add', '--files', nargs='+', help='Upload files to AI platforms')
    parser.add_argument('--demo', '--examples', action='store_true', help='Show examples')
    parser.add_argument('--pick', '--only', type=str, help='Filter AI platforms')
    parser.add_argument('--log', action='store_true', help='Open browser, wait for manual sign-in (Ctrl+C to save)')
    parser.add_argument('--pw', action='store_true', help='Use Playwright instead of raw CDP')
    parser.add_argument('--canary', action='store_true', help='(legacy, no-op) automation now picks chrome-beta or -dev automatically')
    parser.add_argument('--status', action='store_true', help='Login status across LLM platforms')
    parser.add_argument('--signin', action='store_true', help='Light auto sign-in for logged-out platforms')
    parser.add_argument('--deepthink', action='store_true', help='Gemini Deep Think (probes /u/0..2 + caches)')
    parser.add_argument('--drfetch', nargs='?', const='', help='Resume DR conversation (URL or latest), extract report; combine with --watch for live')
    parser.add_argument('--watch', action='store_true', help='Poll DR report until stable (used with --drfetch)')
    args, extra = parser.parse_known_args(); args.ask = args.ask or (' '.join(extra) if extra and not args.say else None)

    if args.demo: show_examples(); sys.exit(0)
    if args.pw: globals()['_use_cdp'] = False
    if args.side: globals()['_center_mode'] = False
    if args.hide: globals()['_headless_mode'] = True
    if args.solo: globals()['_copy_profile'] = False
    pasted_content = read_pasted_content() if args.put else None
    if args.doc: pdf_content = extract_pdf_text(args.doc); pasted_content = f"{pasted_content}\n\n{pdf_content}" if pasted_content else pdf_content
    upload_files = [os.path.abspath(f) for f in args.add] if args.add else None
    if upload_files:
        missing = [f for f in upload_files if not os.path.exists(f)]
        if missing: print(f"  ✗ Files not found: {missing}"); sys.exit(1)
        print(f"  → Files to upload: {[os.path.basename(f) for f in upload_files]}")

    try:
        if args.status: status(only=set(args.pick.lower().split(',')) if args.pick else None)
        elif args.signin: signin(only=set(args.pick.lower().split(',')) if args.pick else None)
        elif args.deepthink:
            p = args.say or args.ask
            if not p: print("x need --ask or --say with prompt"); sys.exit(1)
            gemini_deepthink(p)
        elif args.drfetch is not None:
            drfetch(url=args.drfetch or None, watch=args.watch)
        elif args.log: launch_browser_with_positioning(); input("\n✓ Sign in anywhere. Press Enter or Ctrl+C to save & exit.\n")
        elif args.go: google_workflow()
        elif args.deep or (args.say and not args.all):
            prompt = args.say or args.ask or None
            if pasted_content: prompt = f"{prompt}\n\n{pasted_content}" if prompt else pasted_content
            deep_research_workflow(prompt, only=args.pick)
        elif args.all or args.ask or pasted_content or upload_files:
            query = args.say or args.ask
            if args.all and not query and not pasted_content and not upload_files:
                print("\n" + "="*70 + "\n              MULTI-LLM INTERACTIVE MODE\n" + "="*70)
                query = input("\n➤ Enter prompt (or Enter for default): ").strip() or "Write Python async/await demo"
            if not query: query = "Describe each file I uploaded. What do you see?" if upload_files else "Summarize and analyze this document"
            if pasted_content: query = f"{query}\n\n{pasted_content}"
            multi_ai_workflow(query, args.pick, files=upload_files)
        elif args.test: test_navigation_workflow()
        elif args.rank: web_monitor_workflow(args.rank)
        else:
            print("\n╔═══════════════════════════════════════════════════════════════════╗")
            print("║  AGUI - Short commands (all real words ≤4 chars)                 ║")
            print("╠═══════════════════════════════════════════════════════════════════╣")
            print("║  all   8 LLMs parallel   │  ask/say \"text\"  prompts             ║")
            print("║  deep  Deep research     │  pick <names>    filter AIs           ║")
            print("║  go    Google workflow   │  put             stdin input          ║")
            print("║  bing  Bing search       │  doc <file>      PDF text             ║")
            print("║                          │  add <files>     upload files          ║")
            print("║  art   Pixai.art         │  runs N          repeat count         ║")
            print("║  loop  Pixai + 30 bing   │  demo            show examples        ║")
            print("║  log   Sign-in mode      │  rank            web rankings         ║")
            print("║  test  Test navigation   │  side/hide/solo  display options      ║")
            print("╚═══════════════════════════════════════════════════════════════════╝")
            print("\n  Examples:  all                    Interactive 8-LLM mode")
            print("             all ask \"question\"     Query all AIs")
            print("             all pick claude,grok   Filter to specific AIs")
            print("             log                    Sign in, Ctrl+C to save\n")
    finally:
        close_browser()
