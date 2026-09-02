#!/usr/bin/env python3
# experimental — promoted from my/auto/ after working end-to-end on Gemini + Claude.ai.
"""bri — control Chrome/Firefox tabs: open, click, read, run JS. Extension bridge over HTTP long-poll (bypasses page CSP: works on Gemini/Claude.ai/chatgpt
and other strict-CSP LLM UIs). :1234 HTTP (GET /poll next cmd, POST /resp results); :1235 push JSON.

ONE page-side client per browser — two in one tab double-execute every command and interleave
responses one-behind:
  Firefox: WebExtension lib/bri-ext/ — `a bri deploy` builds+installs+restarts; eval+fetch run in
      extension context, immune to page CSP incl. chatgpt.com (Tampermonkey client removed 2026-06-10).
  Chrome:  lib/bri-chrome/ (same protocol).
  No Marionette/CDP: navigator.webdriver + devtools side-channels trip Google sign-in ("browser may
  not be secure"); extension context is a real user. Launch FF WITHOUT -marionette, sign in once.

`a bri` with no args prints the full MENU of shortcuts. EVERY command drives ONE browser — firefox
default; @chrome / @all prefix or BRI_TO env retargets; raw '{json}' too (explicit "to" field wins).

Driving a chat/web UI — NO hardcoded per-site selectors here: they DRIFT and break SILENTLY (a
script acts but cannot perceive — reports ok on a stale selector, types nothing). Drive it as an
AGENT off the generic primitives, confirming each step by screenshot:
  a bri hint [host]        # Set-of-Mark: label every clickable element
  a bri screenshot         # read the labels with vision
  a bri hint-click <code>  # click by label; RE-HINT after each action (menus add items)
  a bri save <url> [note]  # log the resulting chat URL (a bri <N> reopens it)
Discover a flow once (screenshot each step); record goal/step-path/success-oracle/traps in agent
MEMORY and reuse — still confirming every step; re-discover when stale. Recipes live there, not in bri.

Rendered text can LIE on signed-in apps (proven on Keep): visible cards are truncated previews of
only on-screen notes (content-visibility skips the rest), so `text body` MISSES data both ways —
the full set rides in a page JSON blob; parse it, don't scrape the DOM:
    a bri html > /tmp/k.html; python3 lib/bri/keepx.py /tmp/k.html   # notes#node → full bodies
Data layer beats DOM for any such app (gkeepapi/Takeout for Keep; browser html save caps at 200k).

OAuth ("Continue with Google"): the button often sits in shadow DOM or a cross-origin gsi iframe
(accounts.google.com/gsi/iframe/…) unreachable when eval is CSP-blocked — true on Claude.ai, common
on SaaS. Once per provider click it yourself in the FF tab (existing Google session = one click);
cookies persist and the bridge drives every subsequent run unattended.

Notes:
- Custom-element wrappers (rich-textarea, ms-textarea, …) need a descendant combinator
  (`wrapper [contenteditable]`) to reach the inner editable; the bare wrapper has no .value setter.
- Read, don't poke: ONE `bri html` (full DOM) or `text body` call beats selector-by-selector eval
  (slow; eval CSP-blocked on Google + Claude.ai). text=innerText reads only PAINTED nodes —
  content-visibility apps (Keep, Gmail) hide data from it; prefer html or the app's data blob/API.
- One response per connected frame (main + iframes): pick the row whose src is your target origin
  (longest value); ogs/gsi/clients6/RotateCookiesPage "EvalError"/"no response" rows are subframes,
  not failure; ignore them.
Tail full event stream: tail -f /tmp/bri.log
"""
import socket, threading, queue, json, sys, time, os, re, glob, subprocess

PORT, CMD, LOG = 1234, 1235, '/tmp/bri.log'
pollers, pending = [], {}  # pollers: list[(Queue, browser)]  pending: id -> Queue

def _bid(ua):  # browser/version from a User-Agent — one bridge drives Chrome + Firefox at once
    m = re.search(r'Firefox/([\d.]+)', ua)
    if m: return 'firefox/' + m.group(1)
    m = re.search(r'(?:Chrome|Chromium)/([\d.]+)', ua)
    if m: return 'chrome/' + m.group(1)
    return '?'
def _ua(head):  # extract User-Agent from the raw HTTP request head (bytes)
    for h in head.split(b'\r\n'):
        if h.lower().startswith(b'user-agent:'): return h.split(b':', 1)[1].decode(errors='replace').strip()
    return ''
def _chan(head):  # exact browser channel the ext self-reports (X-Bri-Chan) — UA is frozen, hides Nightly/Canary
    for h in head.split(b'\r\n'):
        if h.lower().startswith(b'x-bri-chan:'): return h.split(b':', 1)[1].decode(errors='replace').strip()
    return ''

def log(*a):
    line = ' '.join(str(x) for x in a)
    print(line, flush=True); open(LOG,'a').write(line+'\n')

def http_send(c, status, body=b'', ctype='application/json'):
    h = f'HTTP/1.1 {status}\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\n\r\n'.encode()
    c.send(h+body)

def handle(c, addr):
    req = b''
    while b'\r\n\r\n' not in req:
        ch = c.recv(4096)
        if not ch: c.close(); return
        req += ch
    head, _, rest = req.partition(b'\r\n\r\n')
    method, path, *_ = (head.split(b'\r\n',1)[0].decode(errors='replace').split() + ['',''])
    if method == 'GET' and path == '/poll':
        q = queue.Queue(); entry = (q, _chan(head) or _bid(_ua(head))); pollers.append(entry)   # remember which exact browser this poller is
        try: cmd = q.get(timeout=25)
        except Exception: cmd = None
        try: pollers.remove(entry)
        except Exception: pass
        if cmd: http_send(c, '200 OK', cmd.encode())
        else:   http_send(c, '204 No Content')
        c.close(); return
    if method == 'POST' and path == '/resp':
        clen = 0
        for h in head.split(b'\r\n'):
            if h.lower().startswith(b'content-length:'): clen = int(h.split(b':',1)[1].strip())
        body = rest
        while len(body) < clen:
            ch = c.recv(4096)
            if not ch: break
            body += ch
        m = body.decode(errors='replace')
        log(f'<< {m}')
        try:
            obj = json.loads(m); obj['br'] = obj.get('chan') or _chan(head) or _bid(_ua(head)); m = json.dumps(obj)   # tag which exact browser answered
            rid = obj.get('id')
            if rid in pending: pending[rid].put(m)
        except Exception: pass
        http_send(c, '200 OK'); c.close(); return
    http_send(c, '404 Not Found'); c.close()

def cmd_serve():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    s.bind(('127.0.0.1',CMD)); s.listen(5); log(f'[*] cmd on :{CMD}')
    while True:
        c,_ = s.accept(); d=b''
        while True:
            ch = c.recv(4096)
            if not ch: break
            d += ch
            if b'\n' in d: break
        msg = d.decode(errors='replace').strip()
        if not msg: c.close(); continue
        rid, tgt = None, 'all'
        try:
            cj = json.loads(msg); rid = cj.get('id'); tgt = (cj.get('to') or 'all').lower()
        except Exception: pass
        tgt = {'ff':'firefox','fx':'firefox','nightly':'firefox','chr':'chrome','canary':'chrome'}.get(tgt, tgt)
        hit = lambda b: tgt in ('all','any','*') or b == '?' or b.startswith(tgt)   # target a browser/version; unknown UA ('?') always matches
        tq = [(q,b) for (q,b) in list(pollers) if hit(b)]
        conn = ', '.join(sorted({b for _,b in pollers})) or 'none'
        if rid is not None: pending[rid] = queue.Queue()
        for q,_b in tq: q.put(msg)
        err = '' if tq else f'no {tgt} client (connected: {conn}). FF ext: a bri deploy'
        if rid is None:
            c.send((f'sent to {len(tq)}/{len(pollers)} pollers (target={tgt}; connected: {conn})\n'+(f'  → {err}\n' if err else '')).encode())
        else:
            out, end = [], time.time()+8
            while not err and time.time() < end:   # msg was queued to zero pollers: waiting out the 8s can only ever return nothing
                try: out.append(pending[rid].get(timeout=min(1.5,end-time.time()) if out else end-time.time()))
                except Exception: break
            del pending[rid]
            c.send(('\n'.join(out) or json.dumps({'error': err or 'no response'})).encode()+b'\n')
        c.close()

def main(browser='none'):
    import briext; briext.build()  # (re)generate BOTH extensions from the single source (lib/briext.py) into adata/local/ext — this is the auto-update
    # The :1234/:1235 bridge is browser-AGNOSTIC: one server drives Firefox (bri-ext)
    # and Chrome (bri-chrome) at the same time. So `serve` launches NO browser by default — only
    # Firefox needs a managed (-marionette-free, monitor-pinned) launch via _ff_restart; Chrome is
    # user-launched with a persistent extension. `a bri serve ff` = also (re)start Firefox. This
    # keeps the Chrome and Firefox serve paths from conflicting (no surprise FF, no double-poller).
    hl = browser == 'ffh'   # HEADLESS mode (proven 2026-08-14): same profile+ext+sign-ins, no window — ext connects, eval/open/screenshot paths identical (bridge is display-agnostic; bloomberg incl. its invisible recaptcha rendered fine). EXCLUSIVE with GUI FF: the bridge addresses browsers by UA, so two FF instances would double-execute — `a bri serve ff` switches back.
    ff = hl or browser in ('ff', 'firefox')
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    try: s.bind(('127.0.0.1',PORT))
    except OSError:
        log(f'[*] :{PORT} already running — bridge serves Chrome + Firefox both' + (', restarting FF' if ff else ''))
        if ff: _ff_restart(hl)
        return
    threading.Thread(target=cmd_serve, daemon=True).start()
    s.listen(50); log(f'[*] http on :{PORT} | log: {LOG}')
    if ff: _ff_restart(hl)  # GUI serve also heals an accidental invisible-FF state (dead wayland socket)
    else:  log('[*] no browser launched (serves Chrome + FF both). Chrome: open the browser + focus an http(s) tab to wake bri-chrome, then `a extload reload`. Firefox: `a bri serve ff` (or `a bri deploy`).')
    while True:
        c,addr = s.accept()
        threading.Thread(target=handle, args=(c,addr), daemon=True).start()

def _sock():
    for i in range(50):
        try:
            s=socket.socket(); s.connect(('127.0.0.1',CMD)); return s
        except OSError:
            if not i:
                subprocess.Popen([sys.executable,__file__,'serve'],stdout=-3,stderr=-3,start_new_session=True)
            time.sleep(.1)
    sys.exit('x bridge failed to start')

# Shortcut CLI: thin client that pushes JSON to a running bridge on :1235 and
# prints the response. The raw '{json}' form remains the full API.
_MONP = lambda: os.path.expanduser('~/a/adata/local/bri_monitor.txt')
def _ff_monitor(): return open(_MONP()).read().strip() if os.path.exists(_MONP()) else ''
def _ff_move(mon):
    # Event-driven: subscribe to window events; on focus of firefox-nightly (window is mapped &
    # selectable by then) move by con_id. "new" fires too early — selector hits "No matching node".
    import subprocess as sp
    sock = os.environ.get('SWAYSOCK') or (sorted(glob.glob(f'/run/user/{os.getuid()}/sway-ipc.*.sock')) or [''])[0]
    if not sock: return
    p = sp.Popen(['swaymsg','-s',sock,'-m','-t','subscribe','["window"]'], stdout=sp.PIPE, stderr=sp.DEVNULL, text=True)
    for ln in p.stdout:
        try: ev = json.loads(ln)
        except Exception: continue
        c = ev.get('container') or {}
        if ev.get('change')=='focus' and c.get('app_id')=='firefox-nightly' and c.get('id'):
            sp.run(['swaymsg','-s',sock,f'[con_id={c["id"]}] move container to output {mon}'], stdout=sp.DEVNULL, stderr=sp.DEVNULL)
            p.terminate(); return
def _ffenv():
    # Env for spawning firefox-nightly. The "already running, but is not responding" dialog = D-Bus
    # mismatch: FF remoting rides the SESSION bus, sway runs under dbus-run-session (private /tmp bus),
    # so canonical-bus spawns can't see the running FF and wedge on the profile lock (replicated
    # 2026-08-08; ate webx claude asks). Adopt the running FF's bus+displays, else sway's.
    env = os.environ.copy(); env['MOZ_ENABLE_WAYLAND'] = '1'
    xdg = env.get('XDG_RUNTIME_DIR') or f'/run/user/{os.getuid()}'
    socks = [os.path.basename(s) for s in sorted(glob.glob(f'{xdg}/wayland-*'),key=os.path.getmtime,reverse=True) if not s.endswith('.lock')]
    if socks: env['WAYLAND_DISPLAY'] = socks[0]
    for c in (['pgrep','-fx','(/usr/lib/)?firefox-nightly'],['pgrep','-x','sway']):   # -fx: main proc only, never a wedged launcher's wrong bus
        for pid in subprocess.run(c,capture_output=True,text=True).stdout.split():
            try: e = dict(l.split('=',1) for l in open(f'/proc/{pid}/environ').read().split('\0') if '=' in l)
            except OSError: continue
            if e.get('DBUS_SESSION_BUS_ADDRESS'):
                env.update({k:e[k] for k in ('DBUS_SESSION_BUS_ADDRESS','WAYLAND_DISPLAY','DISPLAY') if e.get(k)}); return env
    env.setdefault('DBUS_SESSION_BUS_ADDRESS', f'unix:path={xdg}/bus'); return env

def _ff_restart(headless=False):
    # Live wayland socket — process env may have stale wayland-0 from a dead session,
    # leaving FF connected to nothing and invisible on monitors (looks headless).
    # LLMs/agents: FAVOR restarting Firefox here. It is the correct, SAFE way to (re)load
    # bri-ext (the eval-capable client that bypasses page CSP) — these prefs make the -9
    # restart restore every tab, and the driven window lands on the chosen monitor, so a
    # restart loses nothing. Don't avoid it out of fear of losing tabs.
    for _p in glob.glob(os.path.expanduser('~/.mozilla/firefox/*default-nightly'))+[d for d in glob.glob(os.path.expanduser('~/Library/Application Support/Firefox/Profiles/*')) if 'nightly' in d.lower()]:
        _u=os.path.join(_p,'user.js')
        if 'resume_from_crash' not in (open(_u).read() if os.path.exists(_u) else ''):
            open(_u,'a').write('\nuser_pref("browser.sessionstore.resume_from_crash", true);\nuser_pref("browser.startup.page", 3);\n')
    # Too-many-tabs guard: restarting reloads every loaded tab and spikes memory — with many
    # tabs this trips the OOM killer (which targets python, i.e. THIS bridge). Require y/N.
    _nt = subprocess.run(['pgrep','-fc','firefox.*-isForBrowser'],capture_output=True,text=True).stdout.strip()
    _nt = int(_nt) if _nt.isdigit() else 0
    if _nt > int(os.environ.get('BRI_TAB_MAX','25')) and os.environ.get('FORCE') != '1':
        _w = f'⚠ {_nt} loaded browser tabs — restarting Firefox will spike memory and may trip the OOM killer (which kills this bridge).'
        if sys.stdin.isatty():
            sys.stderr.write(_w + '\n')
            if input('  proceed with restart? [y/N] ').strip().lower() != 'y':
                print('aborted (set FORCE=1 to skip this check)'); return
        else:
            sys.stderr.write(_w + '\n  LLM/agent: PAUSE here and ASK THE USER what to do (close tabs first? proceed anyway?).\n'
                                  '  Do NOT auto-proceed; only re-run with FORCE=1 after the user says so.\n')
            return
    subprocess.run(['pkill','-9','-f','^(/usr/lib/)?firefox-nightly'],stdout=-3,stderr=-3); time.sleep(1)  # anchored: unanchored matched vmtouch-firefox-nightly's cmdline (its args are FF paths) and SIGKILLed it every restart
    env = _ffenv()   # post-pkill: adopts sway's bus so the next manual launch hands off, not dialogs
    m = _ff_monitor()
    # fork (not thread) — subscribe must survive client process exit, before FF Popen so window::new isn't missed.
    if m and not headless and os.fork() == 0:
        os.setsid(); _ff_move(m); os._exit(0)
    subprocess.Popen(['firefox-nightly']+(['--headless']if headless else[]),env=env,stdout=-3,stderr=-3,start_new_session=True)

def _mon():
    def run(c): return subprocess.run(c, capture_output=True, text=True).stdout
    pid = (run(['pgrep','-f',r'bri\.py$']).strip().split('\n') or [''])[0]
    if pid: print(f'bri.py   pid={pid:<6} {run(["ps","-o","pcpu,pmem,rss,etime","-p",pid]).strip().split(chr(10))[-1]}')
    ff = [p for p in run(['pgrep','-f','Firefox Nightly']).split() if p]
    if ff:
        tabs = sum(1 for l in run(['pgrep','-fl','Firefox Nightly.app.*-isForBrowser']).split('\n') if l.strip())
        rss = sum(int(x) for x in run(['ps','-o','rss=','-p',','.join(ff)]).split() if x.isdigit())
        cpu = sum(float(x) for x in run(['ps','-o','%cpu=','-p',','.join(ff)]).split() if x.replace('.','').isdigit())
        print(f'firefox  procs={len(ff):<3} tabs={tabs:<3} RAM={rss/1024/1024:.1f}GB CPU={cpu:.0f}%')
    if os.path.exists(LOG): print(f'bri.log  {os.path.getsize(LOG)/1024:.0f}KB')
    print(f'/poll    active long-poll holds: {max(0, len(run(["lsof","-iTCP:1234","-sTCP:ESTABLISHED"]).split(chr(10)))-2)}')

# DON'T USE. Firefox+bri-ext is strictly better for automation: no CSP fights,
# no per-Chrome-version flag whack-a-mole, sideloadable unsigned extensions.
# Kept so you don't have to import 30k tokens of agui to poke Chrome.
# Requires: chrome --remote-debugging-port=9222 --remote-allow-origins='*'
def cdp(method, params=None, _s=[None, 0]):
    if _s[0] is None:
        import websocket, urllib.request as u
        _s[0] = websocket.create_connection(json.loads(u.urlopen('http://127.0.0.1:9222/json').read())[0]['webSocketDebuggerUrl'])
    _s[1] += 1; i = _s[1]
    _s[0].send(json.dumps({'id':i,'method':method,'params':params or {}}))
    while (r := json.loads(_s[0].recv())).get('id') != i: pass
    return r.get('result', {})

# --- selector highlighter (Set-of-Mark) ----------------------------------------
# Drift-proof alternative to hardcoded CSS selectors: label every clickable element
# with a code, screenshot, let a vision model pick the code, then click by code.
#   a bri hint [host]          overlay labels + return code=text map (host filters tabs)
#   a bri hint-click <CODE> [host]   re-enumerate (same order) and click that element
# Both walk open shadow roots and use the identical enumeration so codes are stable.
_HL_SEL = ('a[href],button,input,textarea,select,summary,[role=button],[role=menuitem],'
  '[role=menuitemradio],[role=menuitemcheckbox],[role=tab],[role=switch],[role=option],'
  '[onclick],[contenteditable=""],[contenteditable="true"],[tabindex]:not([tabindex="-1"])')
_HL_ENUM = ('const S=__SEL__;const seen=new Set(),V=[];const W=r=>{r.querySelectorAll(S).forEach(e=>{'
  'if(!seen.has(e)){seen.add(e);V.push(e)}});r.querySelectorAll("*").forEach(e=>e.shadowRoot&&W(e.shadowRoot))};'
  'W(document);const Z=V.filter(e=>{const r=e.getBoundingClientRect();if(r.width<6||r.height<6)return 0;'
  'if(r.bottom<0||r.top>innerHeight||r.right<0||r.left>innerWidth)return 0;const c=getComputedStyle(e);'
  'return c.visibility!="hidden"&&c.display!="none"&&c.pointerEvents!="none"});')
_HL_HINT = ('(()=>{__GUARD____ENUM__document.querySelectorAll(".__bh").forEach(n=>n.remove());'
  'const cd=i=>{const A="ABCDEFGHIJKLMNOPQRSTUVWXYZ";return i<26?A[i]:A[(i/26|0)-1]+A[i%26]};'
  'const o=Z.map((e,i)=>{const c=cd(i),r=e.getBoundingClientRect(),b=document.createElement("div");'
  'b.className="__bh";b.textContent=c;b.style.cssText="position:fixed;left:"+Math.max(0,r.left)+"px;top:"+'
  'Math.max(0,r.top)+"px;z-index:2147483647;background:#ff0;color:#000;font:bold 10px monospace;'
  'padding:0 2px;border:1px solid #000;pointer-events:none;line-height:12px";document.body.appendChild(b);'
  'return c+"="+(e.innerText||e.getAttribute("aria-label")||e.getAttribute("placeholder")||e.tagName)'
  '.trim().replace(/\\s+/g," ").slice(0,30)});return o.length+" marks: "+o.join(" | ")})()')
_HL_CLICK = ('(()=>{__GUARD____ENUM__const c=__CODE__;'
  'const i=c.length<2?c.charCodeAt(0)-65:(c.charCodeAt(0)-64)*26+(c.charCodeAt(1)-65);'
  'const e=Z[i];if(!e)return"no element "+c;const r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2,'
  'o={bubbles:1,cancelable:1,clientX:x,clientY:y,button:0,buttons:1,view:window,pointerType:"mouse",isPrimary:1};'
  '["pointerdown","mousedown","pointerup","mouseup","click"].forEach(t=>e.dispatchEvent('
  'new(t[0]=="p"?PointerEvent:MouseEvent)(t,o)));return"clicked "+c+" = "+'
  '(e.innerText||e.getAttribute("aria-label")||e.tagName).trim().slice(0,30)})()')
def _hl(host='', code=None):
    # `host` is any URL substring — a real host (chatgpt.com) OR a job tag (brijob=7) so
    # parallel jobs each get their own tab: open chatgpt.com/#brijob=7, drive with `hint brijob=7`.
    g = ('if(window.top!=window)return"skip:iframe";if(!location.href.includes('+json.dumps(host)+'))return"skip:"+location.href;') \
        if host else 'if(window.top!=window)return"skip:iframe";if(!document.hasFocus())return"skip:unfocused";'
    body = (_HL_HINT if code is None else _HL_CLICK).replace('__GUARD__', g).replace('__ENUM__', _HL_ENUM.replace('__SEL__', json.dumps(_HL_SEL)))
    return body if code is None else body.replace('__CODE__', json.dumps(code.upper()))

# --- link fan-out (human deep research): links/openall/tabs/grabs/closeall/research ---
def _send(j, to=None):  # push one command to :1235, return the parsed response objects
    j.setdefault('id', int(time.time()*1000) % 10**9)
    if to and to != 'all': j['to'] = to
    s = _sock()
    s.sendall((json.dumps(j)+'\n').encode()); buf = b''
    while (ch := s.recv(1<<16)): buf += ch
    out = []
    for ln in buf.decode(errors='replace').splitlines():
        try: out.append(json.loads(ln))
        except Exception: pass
    return out
_SJUNK = re.compile(r'//([\w.-]*\.)?(google\.\w+|bing\.com|duckduckgo\.com|duck\.ai|gstatic\.com|msn\.com|microsoft\.com|mozilla\.org|apps\.apple\.com|creativecommons\.org|insideduckduckgo\.\w+)/', re.I)
def _unwrap(u):  # bing /ck/a + ddg /l/ + google /url redirect wrappers -> the real target URL
    from urllib.parse import urlparse, parse_qs, unquote
    p = urlparse(u); q = parse_qs(p.query); h = p.netloc.lower()
    if h.endswith('bing.com') and p.path.startswith('/ck/'):
        v = q.get('u', [''])[0]
        try: return __import__('base64').urlsafe_b64decode(v[2:]+'==').decode() if v[:2] == 'a1' else None
        except Exception: return None
    if h.endswith('duckduckgo.com') and p.path.startswith('/l/'): return unquote(q.get('uddg', [''])[0]) or None
    if h.endswith('google.com') and p.path == '/url': return q.get('q', [''])[0] or None
    return u
def _links(match, to, raw=False):  # harvest links from tabs matching <match>: unwrap redirects, drop engine chrome, dedupe
    seen, out = set(), []
    for r in _send({'action': 'links', 'match': match, 'host': match}, to):
        for h, t in (r.get('value') or []):
            u = h if raw else _unwrap(h)
            if not u or not u.startswith('http') or (not raw and _SJUNK.search(u)): continue
            k = re.sub(r'[?#].*', '', u).rstrip('/')
            if k not in seen: seen.add(k); out.append((u, t))
    return out
def _openall(urls, to):  # open as bg tabs (no focus steal); capped + RAM-guarded; ~1.5s reply window per open = stagger
    mx = int(os.environ.get('BRI_OPEN_MAX', '20'))
    if len(urls) > mx: sys.stderr.write(f'! capped {len(urls)} -> {mx} tabs (BRI_OPEN_MAX)\n'); urls = urls[:mx]
    for i, u in enumerate(urls):
        try:
            if int(next(l for l in open('/proc/meminfo') if 'MemAvailable' in l).split()[1]) < 3*1024*1024:
                sys.stderr.write(f'x RAM guard (<3G available): stopped at {i}/{len(urls)}\n'); break
        except Exception: pass
        _send({'action': 'open', 'url': u, 'bg': 1, 'fresh': 1}, to); print(f'+ {u}', flush=True)

def client(args):
    to = args[0][1:] if args and args[0].startswith('@') else ''   # @firefox / @chrome / @all / @firefox/145 — target a browser (CLI default: firefox)
    if to: args = args[1:]
    to_exp = bool(to or os.environ.get('BRI_TO'))   # explicit target — read-only listings default to all browsers instead
    to = to or os.environ.get('BRI_TO') or 'firefox'
    a = args[0] if args else ''
    if a == 'cdp':
        if len(args) < 2: print("bri cdp <url> | bri cdp eval <js>  (suffer-mode: needs chrome --remote-debugging-port=9222 --remote-allow-origins='*')"); return
        if args[1] == 'eval' and len(args) > 2:
            print(cdp('Runtime.evaluate', {'expression': ' '.join(args[2:]), 'returnByValue': True}).get('result', {}).get('value'))
        else: cdp('Page.navigate', {'url': args[1]}); print(f'nav: {args[1]}')
        return
    if a == 'restart': _ff_restart(); print('restarted Firefox Nightly'); return
    if a == 'new':  # open a new job tab tagged in the URL; drive it with `a bri hint brijob=<id>`
        if len(args) < 2: sys.stderr.write('usage: a bri new <id> [url]   (default url: chatgpt.com)\n'); sys.exit(1)
        jid, url = args[1], (args[2] if len(args) > 2 else 'chatgpt.com')
        if not url.startswith(('http://','https://')): url = 'https://' + url
        url += ('&' if '#' in url else '#') + 'brijob=' + jid
        subprocess.Popen(['firefox-nightly','--new-tab',url], env=_ffenv(), stdout=-3, stderr=-3, start_new_session=True)
        sys.stdout.write(f'+ job tab: {url}\n  drive: a bri hint brijob={jid} | hint-click <C> brijob={jid} | save <url>\n'); return
    if a == 'mon':     _mon(); return
    if a in ('grab', 'copy'):  # select-all+copy equivalent: full innerText (or --html) of the tab whose URL contains <host>
        host = next((x for x in args[1:] if not x.startswith('--')), '')
        prop = 'document.documentElement.outerHTML' if '--html' in args else 'document.body.innerText'
        guard = (f'if(!location.href.toLowerCase().includes({json.dumps(host.lower())}))return null;'
                 if host else 'if(!document.hasFocus())return null;')   # no host → the focused tab
        code = f'(()=>{{if(window.top!==window)return null;{guard}return {prop};}})()'
        best = ''   # many tabs reply (most null); the target tab's innerText is the longest
        rs = _send({'action': 'eval', 'code': code}, to)
        for r in rs:
            v = r.get('value')
            if isinstance(v, str) and len(v) > len(best): best = v
        if best: sys.stdout.write(best + '\n')
        else: sys.stderr.write(f'x no text — is the {host or "focused"} tab open, logged in, and active? (discarded tabs read blank until clicked)'
                               f' [target={to}; answered: {", ".join(sorted({r["br"] for r in rs if r.get("br")})) or "none"} — wrong browser? @chrome/@all]\n'); sys.exit(1)
        return
    if a == 'links':  # result links of tabs whose URL has <match>: SERP redirects unwrapped, engine chrome dropped, deduped
        if len(args) < 2: sys.stderr.write('usage: a bri links <url-substring> [--raw]\n'); sys.exit(1)
        for u, t in _links(args[1], to, '--raw' in args): print(f'{u}\t{t}')
        return
    if a == 'openall':  # open every URL (args, or stdin lines: first token) as background tabs
        urls = [w for w in args[1:] if w.startswith('http')] or \
               [l.split()[0] for l in sys.stdin if l.split() and l.split()[0].startswith('http')]
        _openall(urls, to); return
    if a == 'tabs':  # id/status/url/title of every tab, BOTH browsers unless @targeted, rows tagged — background-side, so it SEES error/discarded tabs text+grab can't
        rs = _send({'action': 'tabs', 'match': args[1] if len(args) > 1 else ''}, to if to_exp else 'all')
        n = 0
        for r in rs:
            for i, w, st, u, ti in (r.get('value') or []): n += 1; print(f'{(r.get("chan") or r.get("br","?")):<23} w{w:<3}{i:>4} {st:<9} {u}  [{ti}]')
        if not n: sys.stderr.write(f'x no tab rows [answered: {", ".join(sorted({r["br"] for r in rs if r.get("br")})) or "none"} — browser ext missing the tabs action? a briext + browser restart deploys it]\n'); sys.exit(1)
        return
    if a == 'grabs':  # full innerText of EVERY tab matching <match> (grab = one best tab), ==== <url> headers
        if len(args) < 2: sys.stderr.write('usage: a bri grabs <url-substring>\n'); sys.exit(1)
        for r in _send({'action': 'text', 'sel': 'body', 'match': args[1], 'host': args[1]}, to):
            if isinstance(r.get('value'), str): print(f'==== {r.get("src", "?")}\n{r["value"]}')
        return
    if a == 'closeall':  # close every tab whose URL contains <match> (cleanup after openall/research)
        if len(args) < 2: sys.stderr.write('usage: a bri closeall <url-substring>\n'); sys.exit(1)
        for r in _send({'action': 'closeall', 'match': args[1]}, to):
            if r.get('ok'): print(f'closed {r["value"]} tabs')
        return
    if a == 'research':  # human deep research: query -> google+bing+ddg SERPs -> union of result links -> open ALL as tabs
        if len(args) < 2: sys.stderr.write('usage: a bri research <query...>\n'); sys.exit(1)
        from urllib.parse import quote_plus
        q = quote_plus(' '.join(args[1:]))
        for e in (f'www.google.com/search?q={q}', f'www.bing.com/search?q={q}', f'duckduckgo.com/?q={q}'):
            _send({'action': 'open', 'url': 'https://' + e, 'bg': 1, 'fresh': 1}, to)
        print('engines open, rendering...'); time.sleep(5)
        ls = _links('?q=' + q, to) or (time.sleep(5) or _links('?q=' + q, to))   # all 3 SERPs share the ?q= marker
        for u, t in ls: print(f'{u}\t{t}')
        print(f'-- {len(ls)} unique results, opening all --')
        _openall([u for u, _t in ls], to)
        print(f"next: a bri grabs <url-substring> | a bri tabs | a bri closeall '?q={q[:40]}'")
        return
    if a == 'save':  # generic URL log (replaces the per-site dr.sh appenders); a bri <N> reopens
        import datetime, urllib.parse
        if len(args) < 2: sys.stderr.write('usage: a bri save <url> [note...]\n'); sys.exit(1)
        src = urllib.parse.urlparse(args[1]).hostname or 'web'
        line = f'{datetime.datetime.now().astimezone().isoformat(timespec="seconds")} {src} {args[1]} {" ".join(args[2:])}\n'
        p = os.path.expanduser('~/a/adata/git/urls.txt'); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'a').write(line); sys.stdout.write('+ saved → adata/git/urls.txt\n' + line); return
    if a == 'screen':
        p = _MONP(); os.makedirs(os.path.dirname(p), exist_ok=True)
        if len(args) > 1:
            v = '' if args[1] == '-' else args[1]
            open(p,'w').write(v); print(f'monitor = {v or "(cleared)"}')
            _ff_restart()
        else:
            cur = open(p).read().strip() if os.path.exists(p) else ''
            print(f'current: {cur or "(none, default)"}')
            sock = (sorted(glob.glob(f'/run/user/{os.getuid()}/sway-ipc.*.sock')) or [''])[0]
            if sock:
                r = subprocess.run(['swaymsg','-s',sock,'-t','get_outputs','--raw'],capture_output=True,text=True)
                if r.returncode==0:
                    for o in json.loads(r.stdout):
                        rc=o['rect']; print(f"  {o['name']:<10} {o.get('make','')[:10]:<10} {o.get('model','')[:18]:<18} pos=({rc['x']},{rc['y']}) {rc['width']}x{rc['height']}")
            print('set: a bri screen <name>   clear: a bri screen -')
        return
    if a.isdigit():  # `a bri <N>` opens the Nth recent research URL in default browser
        p = os.path.expanduser('~/a/adata/git/urls.txt')
        if not os.path.exists(p): print('no urls.txt'); return
        ln = [l.strip() for l in open(p) if l.strip()][-4:]; n = int(a)
        if not (1 <= n <= len(ln)): print(f'usage: a bri 1..{len(ln)}'); return
        url = next((w for w in ln[n-1].split(' ') if w.startswith('http')), None)
        if not url: print('no url in that entry'); return
        subprocess.Popen(['xdg-open', url], stdout=-3, stderr=-3); print(f'+ {url}'); return
    if a == 'tail':
        # Record a session for an LLM to compile into automation. /tmp/bri.log
        # is every cmd+response that passes the bridge — `tail -F` it into a
        # timestamped (or named) file under adata/tmp/ while the user drives a
        # workflow manually via `a bri <cmd>`. Then point an LLM at the file:
        # "turn this into a replay script". The recording IS the ground truth.
        import datetime as dt
        d = os.path.expanduser('~/a/adata/tmp'); os.makedirs(d, exist_ok=True)
        name = args[1] if len(args) > 1 else dt.datetime.now().strftime('%Y%m%d-%H%M%S')
        f = f'{d}/bri-{name}.log'
        print(f'+ recording → {f}\n  drive workflow in another shell with `a bri <cmd>` then Ctrl-C\n')
        os.execvp('sh', ['sh', '-c', f'tail -F -n 0 {LOG} | tee {f!r}']); return
    if a == 'deploy':  # zero-click rebuild+install of the FF ext + Firefox restart
        import shutil, briext
        briext.build()  # regenerate from single source
        extdir = os.path.join(briext.OUT, 'bri-ext')
        subprocess.check_call(['zip','-jq', f'{extdir}/a-bridge.xpi']
                              + [f for f in glob.glob(f'{extdir}/*') if not f.endswith('.xpi')])
    if a == 'get':  # save what the ACTIVE tab shows → ~/Downloads (or [out]): largest <video>/<img> (or css <sel>). Bytes fetched IN the tab (its session; blob:/data: too) ride the bridge base64; an http video or a MediaSource blob (YouTube) goes to yt-dlp with the tab's cookies
        import base64, urllib.parse
        sel = args[1] if len(args) > 1 else 'video,img'
        code = ('(async()=>{if(top!==self||document.hidden)return null;const e=[...document.querySelectorAll(%s)].sort((a,b)=>b.offsetWidth*b.offsetHeight-a.offsetWidth*a.offsetHeight)[0];if(!e)return null;const u=e.currentSrc||e.src,r=[u,location.href];'
                'if(e.tagName=="VIDEO"&&/^http/.test(u))return r;try{const b=await(await fetch(u)).blob();r.push(await new Promise(z=>{const f=new FileReader();f.onload=()=>z(f.result);f.readAsDataURL(b)}))}catch(x){}return r})()') % json.dumps(sel)
        v = next((r['value'] for r in _send({'action': 'eval', 'code': code}, to) if isinstance(r.get('value'), list)), None)
        if not v: sys.stderr.write(f'x no <{sel}> on the active tab\n'); sys.exit(1)
        dl, h = os.path.expanduser('~/Downloads'), v[0].startswith('http')
        if len(v) > 2:
            out = args[2] if len(args) > 2 else f'{dl}/' + ((urllib.parse.unquote(os.path.basename(v[0].split('?')[0])) if h else '') or f'bri-{int(time.time())}.' + v[2].split(';')[0].split('/')[1])
            open(out, 'wb').write(base64.b64decode(v[2].split(',', 1)[1])); print(f'{out}  ← {v[0]}'); return
        prof = glob.glob(os.path.expanduser('~/.mozilla/firefox/*default-nightly'))   # the agent FF's cookies
        os.execvp('yt-dlp', ['yt-dlp', '--js-runtimes', 'node', '-P', dl] + (['--cookies-from-browser', 'firefox:' + prof[0]] if prof else []) + (['-o', args[2]] if len(args) > 2 else []) + [v[0] if h else v[1]])
        # Prefer Nightly (active); fall back to dev/release.
        cands = glob.glob(os.path.expanduser('~/Library/Application Support/Firefox/Profiles/*')) \
              + glob.glob(os.path.expanduser('~/.mozilla/firefox/*default*'))
        prof = [p for p in cands if 'nightly' in p.lower()] or \
               [p for p in cands if p.endswith('default-dev')] or \
               [p for p in cands if p.endswith('default-release')] or cands
        if not prof: sys.stderr.write('x no FF profile found\n'); sys.exit(1)
        os.makedirs(f'{prof[0]}/extensions', exist_ok=True)
        shutil.copy(f'{extdir}/a-bridge.xpi', f'{prof[0]}/extensions/a-bridge@seanpatten.xpi')
        print(f'  → {prof[0]}/extensions/')
        shutil.rmtree(f'{prof[0]}/startupCache', ignore_errors=True)   # else FF re-runs STALE ext bytecode: 3 correct deploys silently no-op'd (2026-08-24)
        _ff_restart()
        print(f'deployed bri-ext v{json.load(open(f"{extdir}/manifest.json"))["version"]}'); return
    if a.startswith('{'):  # raw JSON gets the same default target — verbatim passthrough broadcast to BOTH browsers (every webx eval double-ran, 2026-08-02); explicit 'to' wins
        try:
            jr = json.loads(a)
            if to != 'all': jr.setdefault('to', to)
            msg = json.dumps(jr)
        except ValueError: msg = a
    else:
        RID = int(time.time()*1000) % 10**9          # unique per invocation — id:1 reuse cross-routed slow frames' replies into the NEXT command's window
        if a.startswith(('http://','https://')): j = {'action':'navigate','url':a}  # no id: page unloads
        elif a=='text':  j = {'id':RID,'action':'text','sel':args[1] if len(args)>1 else 'body'}
        elif a=='click': j = {'id':RID,'action':'click','sel':args[1]}
        elif a=='type':  j = {'id':RID,'action':'type','sel':args[1],'text':args[2]}
        elif a=='keys':  j = {'id':RID,'action':'keys','sel':args[1],'keys':args[2]}
        elif a=='url':   j = {'id':RID,'action':'url'}
        elif a=='hint':  j = {'id':RID,'action':'eval','code':_hl(args[1] if len(args)>1 else '')}
        elif a in ('hint-click','hc'):
            if len(args)<2: sys.stderr.write('usage: a bri hint-click <CODE> [host]\n'); sys.exit(1)
            j = {'id':RID,'action':'eval','code':_hl(args[2] if len(args)>2 else '', args[1])}
        elif a=='screenshot':  # avoid when text/html/url can serve — the MENU entry carries the policy
            import base64
            out = args[1] if len(args)>1 else f'/tmp/bri-{int(time.time())}.png'
            for r in _send({'action':'screenshot'}, to):
                v = r.get('value','')
                if isinstance(v,str) and v.startswith('data:image/'):
                    open(out,'wb').write(base64.b64decode(v.split(',',1)[1]))
                    print(out); return
            sys.stderr.write('x no image returned (ext loaded? a bri deploy)\n'); sys.exit(1)
        else: sys.stderr.write(MENU+'\n'); sys.exit(1)   # one maintained list, not a second stale copy
        if to != 'all': j['to'] = to   # CLI targets firefox by default; @all (or BRI_TO=all) broadcasts to every browser
        msg = json.dumps(j)
    s = _sock(); s.sendall((msg+'\n').encode()); r = b''
    while (ch := s.recv(1<<16)): r += ch
    sys.stdout.write(r.decode(errors='replace'))
    sys.exit(r.startswith(b'{"error"'))   # rc, not just text: hub jobs and scripts cannot see a dead target otherwise

MENU = """a bri <cmd>     extension bridge to Firefox/Chrome — ONE target per cmd: firefox default, @chrome/@all prefix retargets
  serve [ff]       start bridge (:1234 http, :1235 cmd) — serves Chrome+FF both; add 'ff' to also launch Firefox on monitor
  deploy           rebuild lib/bri-ext xpi + install + restart FF (zero-click)
  restart          quit + relaunch FF Nightly
  screen [name|-]  list outputs / pin FF to sway output (no arg=show, -=clear)
  mon              bri.py + FF CPU/RAM/tabs snapshot
  tail [name]      record bridge traffic → adata/tmp/bri-<name>.log (Ctrl-C stops)
  <N>              open Nth recent research URL (1..4) in default browser
  <url>            navigate (http/https detected)
  text [sel]       read body or selected element (sel default: body)
  click <sel>      click element
  type <sel> <s>   type into element (handles contenteditable)
  keys <sel> <k>   dispatch keydown/keyup (e.g. Enter)
  url              current URL
  grab [host]      select-all+copy equiv: full innerText of the tab whose URL has <host> (--html=DOM, no host=focused)
  links <match>    result links of tabs whose URL has <match> (SERP redirects unwrapped+deduped; --raw=verbatim)
  openall [u...|-] open URLs (args/stdin) as bg tabs (BRI_OPEN_MAX=20, RAM-guarded)
  tabs [match]     id/status/url/title of every tab — sees error/discarded tabs grab can't
  grabs <match>    innerText of EVERY tab matching <match> (grab = one best)
  closeall <match> close every tab whose URL contains <match>
  research <q...>  google+bing+ddg -> open every unique result (human deep research)
  new <id> [url]   open a job tab tagged #brijob=<id> (default chatgpt.com) for parallel jobs
  hint [match]     label clickable els on tabs whose URL contains <match> (host or brijob=<id>)
  hint-click <C>   click element C (re-enumerates; optional [match] filter, same as hint)
  save <url> [nt]  log a research/chat URL → urls.txt (a bri <N> reopens)
  screenshot [p]   PNG of active tab → p (default /tmp/bri-<ts>.png)
                   ! avoid if possible — prefer text/html/url (text is the
                     artifact, PNG is lossy + heavy + needs a vision model)
  '{json}'         raw passthrough — full 9-action protocol (default-targeted too; a "to" field overrides)
first run — Firefox: a bri serve ff then a bri deploy (serve ffh = HEADLESS FF, same profile/sign-ins, no window; serve ff switches back)   ·   Chrome: a bri serve then load bri-chrome (a extload)"""

  get [sel] [out]  save what the active tab shows → ~/Downloads: largest <video>/<img> (or css sel); img fetched in-tab (its session), video via yt-dlp with the tab's cookies
if __name__=='__main__':
    args = sys.argv[1:]
    if args and args[0] == 'bri': args = args[1:]  # `a bri …` passes cmd name as argv[1]
    if not args:
        up = subprocess.run(['ss','-ltn','sport = :1234'],capture_output=True,text=True).stdout
        print(f"[{'running' if ':1234' in up else 'stopped'}] :1234")
        if ':1234' in up:  # connected browsers + default target were invisible — dual-browser confusion 2026-08-02
            s = _sock(); s.sendall(b'{}\n'); r = s.recv(4096).decode(errors='replace'); s.close()
            m = re.search(r'connected: [^)\n]*', r)
            print(f"  target: firefox by default (@chrome @all or BRI_TO override) · {m.group(0) if m else '?'}")
        # Recent research URLs — logged generically via `a bri save <url>` (any site).
        # Numbered → `a bri <N>` opens URL N. URL on its own line so terminals
        # that auto-detect plain URLs make them clickable too.
        try:
            with open(os.path.expanduser('~/a/adata/git/urls.txt')) as f:
                ln = [l.strip() for l in f if l.strip()][-4:]
            if ln:
                print("\nrecent research (a bri <N> opens):")
                for i, l in enumerate(ln, 1):
                    p = l.split(' ', 3)
                    if len(p) < 3: continue
                    is_url = p[2].startswith('http')
                    shown = f'\033[4;36m{p[2]}\033[0m' if is_url else f'\033[33m({p[2]})\033[0m'
                    note = p[3] if len(p)>=4 else ''
                    print(f"  [{i}] {shown}")
                    print(f"      {p[0]} {p[1]} — {note}")
        except FileNotFoundError: pass
        print(f"\n{MENU}")
    elif args[0] == 'serve': main(args[1] if len(args) > 1 else 'none')
    else: client(args)
