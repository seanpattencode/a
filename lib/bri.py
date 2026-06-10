#!/usr/bin/env python3
# experimental — promoted from my/auto/ after working end-to-end on Gemini + Claude.ai.
"""bri — extension bridge via HTTP long-poll (bypasses page CSP, works on
Gemini/Claude.ai/chatgpt/other strict-CSP LLM UIs). Listens :1234 (HTTP:
GET /poll for next cmd, POST /resp for results); :1235 (push JSON).

ONE page-side client per browser — never two in one tab (a second client makes
every command double-execute and responses interleave one-behind):
  Firefox: WebExtension at lib/bri-ext/ — `a bri deploy` builds + installs +
      restarts. Eval runs in extension context (immune to page CSP, incl.
      chatgpt.com); fetch likewise. Tampermonkey client removed 2026-06-10.
  Chrome:  lib/bri-chrome/ (same /poll + /resp protocol).
  No Marionette/CDP: navigator.webdriver + devtools side-channels get flagged
  by Google sign-in ("browser may not be secure"); extension context is a real
  user. Launch Firefox WITHOUT -marionette → sign into target site once.

Drive shortcuts (bridge must already be running in another process):
  bri <url>          navigate (http/https prefix detected)
  bri text [sel]     read body or selected element (sel defaults to "body")
  bri click <sel>    click element
  bri type <sel> <s> type into element (handles contenteditable like rich-textarea)
  bri keys <sel> <k> dispatch keydown/keyup (e.g. "Enter" to submit)
  bri url            return current URL
  bri deploy         rebuild lib/bri-ext xpi + restart Firefox (zero-click extension bump)
  bri restart        quit + relaunch Firefox Nightly (clears tab leaks)
  bri mon            snapshot bri.py + Firefox CPU/RAM/tabs/connections
  bri '{json}'       raw passthrough — full 9-action protocol, all fields
                     (id, sel, text, code, args, ms, keys, …). Use when a
                     shortcut shape doesn't fit, e.g. eval/wait/html/custom id.

Driving a chat/web UI — do NOT hardcode per-site selectors here. They DRIFT and
break SILENTLY: a script acts but cannot perceive, so it reports ok on a stale
selector and types nothing. Drive it as an AGENT off the generic primitives,
confirming each step by screenshot:
  a bri hint [host]        # Set-of-Mark: label every clickable element
  a bri screenshot         # read the labels with vision
  a bri hint-click <code>  # click by label; RE-HINT after each action (menus add items)
  a bri save <url> [note]  # log the resulting chat URL (a bri <N> reopens it)
Discover a flow once by screenshotting each step; record the working path (goal,
step path, success-oracle, traps) in agent MEMORY and reuse it — but keep
screenshotting and confirming every step, and re-discover if it's stale. Per-site
recipes live in agent memory, not in bri.

Read a signed-in app — rendered text can LIE (proven on Keep):
  The visible cards are truncated previews of only the on-screen notes
  (content-visibility skips the rest), so `text body` MISSES data both ways.
  The full set rides in a page JSON blob → parse it, don't scrape the DOM:
    a bri html > /tmp/k.html      # or browser File>Save As (html caps at 200k)
    python3 lib/bri/keepx.py /tmp/k.html   # notes#node → indexableText, full bodies
  General data layer beats DOM for any such app (gkeepapi/Takeout for Keep).

OAuth sign-in (e.g. "Continue with Google"):
  Some sign-in buttons live in shadow DOM or cross-origin Google Identity
  Services iframes (accounts.google.com/gsi/iframe/…) that aren't reachable
  by querySelector when eval is CSP-blocked — true on Claude.ai, common on
  SaaS apps. Once per provider: click "Continue with Google" yourself in the
  Firefox tab — your existing Google session usually makes it one click —
  then cookies persist and the bridge drives every subsequent run unattended.

Notes:
- Custom-element wrappers (rich-textarea, ms-textarea, …) need a CSS
  descendant combinator (`wrapper [contenteditable]`) to reach the inner
  <div contenteditable>. The bare wrapper has no working .value setter.
- Read, don't poke: grab `bri html` (full DOM) or `text body` in ONE call and
  parse that, vs selector-by-selector eval (slow, and eval is CSP-blocked on
  Google + Claude.ai). But text=innerText reads only PAINTED nodes — apps using
  content-visibility/preview-clipping (Keep, Gmail) hide data from it; prefer
  html, or the app's data blob/API, when the rendered text looks incomplete.
- Each command gets one response per connected frame (main + iframes). Pick the
  row whose src is your target origin — its value is longest. The noise rows
  (ogs/gsi/clients6 proxies, accounts.google.com RotateCookiesPage → CSP
  "EvalError"/"no response") are subframes, not failure; ignore them.
Tail full event stream: tail -f /tmp/bri.log
"""
import socket, threading, queue, json, sys, time

PORT, CMD, LOG = 1234, 1235, '/tmp/bri.log'
pollers, pending = [], {}  # pollers: list[(Queue, browser)]  pending: id -> Queue

import re
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
        q = queue.Queue(); entry = (q, _bid(_ua(head))); pollers.append(entry)   # remember which browser this poller is
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
            obj = json.loads(m); obj['br'] = _bid(_ua(head)); m = json.dumps(obj)   # tag which browser/version answered
            rid = obj.get('id')
            if rid in pending: pending[rid].put(m)
        except Exception: pass
        http_send(c, '200 OK'); c.close(); return
    if method == 'GET' and path == '/bridge.js':  # frozen-shell: bri-chrome SW fetches live bridge logic here each start (see sw.js bridgeSetup) → edit lib/bri-chrome/bridge.js, no repack/re-drag
        import os; body = b''
        try: body = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bri-chrome', 'bridge.js'), 'rb').read()
        except Exception as e: log(f'!! /bridge.js {e}')
        log(f'>> served /bridge.js ({len(body)}b)')
        http_send(c, '200 OK', body, 'application/javascript; charset=utf-8')
        c.close(); return
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
        if rid is None:
            hint = '' if tq else f'  → no {tgt} client (connected: {conn}). FF ext: a bri deploy\n'
            c.send(f'sent to {len(tq)}/{len(pollers)} pollers (target={tgt}; connected: {conn})\n{hint}'.encode())
        else:
            out, end = [], time.time()+8
            while time.time() < end:
                try: out.append(pending[rid].get(timeout=min(1.5,end-time.time()) if out else end-time.time()))
                except Exception: break
            del pending[rid]
            c.send(('\n'.join(out) or '{"error":"no response"}').encode()+b'\n')
        c.close()

def main(browser='none'):
    # The :1234/:1235 bridge is browser-AGNOSTIC: one server drives Firefox (bri-ext)
    # and Chrome (bri-chrome) at the same time. So `serve` launches NO browser by default — only
    # Firefox needs a managed (-marionette-free, monitor-pinned) launch via _ff_restart; Chrome is
    # user-launched with a persistent extension. `a bri serve ff` = also (re)start Firefox. This
    # keeps the Chrome and Firefox serve paths from conflicting (no surprise FF, no double-poller).
    ff = browser in ('ff', 'firefox')
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    try: s.bind(('127.0.0.1',PORT))
    except OSError:
        log(f'[*] :{PORT} already running — bridge serves Chrome + Firefox both' + (', restarting FF' if ff else ''))
        if ff: _ff_restart()
        return
    threading.Thread(target=cmd_serve, daemon=True).start()
    s.listen(50); log(f'[*] http on :{PORT} | log: {LOG}')
    if ff: _ff_restart()  # ensure FF visible on monitors (kills stale headless instance)
    else:  log('[*] no browser launched (serves Chrome + FF both). Chrome: open the browser + focus an http(s) tab to wake bri-chrome, then `a extload reload`. Firefox: `a bri serve ff` (or `a bri deploy`).')
    while True:
        c,addr = s.accept()
        threading.Thread(target=handle, args=(c,addr), daemon=True).start()

# Shortcut CLI: thin client that pushes JSON to a running bridge on :1235 and
# prints the response. The raw '{json}' form remains the full API.
_MONP = lambda: __import__('os').path.expanduser('~/a/adata/local/bri_monitor.txt')
def _ff_monitor(): import os; return open(_MONP()).read().strip() if os.path.exists(_MONP()) else ''
def _ff_move(mon):
    # Event-driven: subscribe to window events; on focus of firefox-nightly (window is mapped &
    # selectable by then) move by con_id. "new" fires too early — selector hits "No matching node".
    import os, glob, subprocess as sp
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
def _ff_restart():
    # Live wayland socket — process env may have stale wayland-0 from a dead session,
    # leaving FF connected to nothing and invisible on monitors (looks headless).
    import os, glob, subprocess, sys
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
    subprocess.run(['pkill','-9','-f','firefox-nightly'],stdout=-3,stderr=-3); time.sleep(1)
    env = os.environ.copy(); xdg = env.get('XDG_RUNTIME_DIR') or f'/run/user/{os.getuid()}'
    socks = [os.path.basename(s) for s in sorted(glob.glob(f'{xdg}/wayland-*'),key=os.path.getmtime,reverse=True) if not s.endswith('.lock')]
    if socks: env['WAYLAND_DISPLAY'] = socks[0]
    env['MOZ_ENABLE_WAYLAND'] = '1'
    m = _ff_monitor()
    # fork (not thread) — subscribe must survive client process exit, before FF Popen so window::new isn't missed.
    if m and os.fork() == 0:
        os.setsid(); _ff_move(m); os._exit(0)
    subprocess.Popen(['firefox-nightly'],env=env,stdout=-3,stderr=-3,start_new_session=True)

def _mon():
    import subprocess, os
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

def client(args):
    import os
    to = args[0][1:] if args and args[0].startswith('@') else ''   # @firefox / @chrome / @all / @firefox/145 — target a browser (CLI default: firefox)
    if to: args = args[1:]
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
        import os, subprocess
        if len(args) < 2: sys.stderr.write('usage: a bri new <id> [url]   (default url: chatgpt.com)\n'); sys.exit(1)
        jid, url = args[1], (args[2] if len(args) > 2 else 'chatgpt.com')
        if not url.startswith(('http://','https://')): url = 'https://' + url
        url += ('&' if '#' in url else '#') + 'brijob=' + jid
        env = os.environ.copy(); env.setdefault('WAYLAND_DISPLAY','wayland-1'); env['MOZ_ENABLE_WAYLAND']='1'
        subprocess.Popen(['firefox-nightly','--new-tab',url], env=env, stdout=-3, stderr=-3, start_new_session=True)
        sys.stdout.write(f'+ job tab: {url}\n  drive: a bri hint brijob={jid} | hint-click <C> brijob={jid} | save <url>\n'); return
    if a == 'mon':     _mon(); return
    if a == 'save':  # generic URL log (replaces the per-site dr.sh appenders); a bri <N> reopens
        import datetime, urllib.parse, os
        if len(args) < 2: sys.stderr.write('usage: a bri save <url> [note...]\n'); sys.exit(1)
        src = urllib.parse.urlparse(args[1]).hostname or 'web'
        line = f'{datetime.datetime.now().astimezone().isoformat(timespec="seconds")} {src} {args[1]} {" ".join(args[2:])}\n'
        p = os.path.expanduser('~/a/adata/git/urls.txt'); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'a').write(line); sys.stdout.write('+ saved → adata/git/urls.txt\n' + line); return
    if a == 'screen':
        import os
        p = _MONP(); os.makedirs(os.path.dirname(p), exist_ok=True)
        if len(args) > 1:
            v = '' if args[1] == '-' else args[1]
            open(p,'w').write(v); print(f'monitor = {v or "(cleared)"}')
            _ff_restart()
        else:
            cur = open(p).read().strip() if os.path.exists(p) else ''
            print(f'current: {cur or "(none, default)"}')
            import subprocess, glob, json as _j
            sock = (sorted(glob.glob(f'/run/user/{os.getuid()}/sway-ipc.*.sock')) or [''])[0]
            if sock:
                r = subprocess.run(['swaymsg','-s',sock,'-t','get_outputs','--raw'],capture_output=True,text=True)
                if r.returncode==0:
                    for o in _j.loads(r.stdout):
                        rc=o['rect']; print(f"  {o['name']:<10} {o.get('make','')[:10]:<10} {o.get('model','')[:18]:<18} pos=({rc['x']},{rc['y']}) {rc['width']}x{rc['height']}")
            print('set: a bri screen <name>   clear: a bri screen -')
        return
    if a.isdigit():  # `a bri <N>` opens the Nth recent research URL in default browser
        import os, subprocess
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
        import os, datetime as dt
        d = os.path.expanduser('~/a/adata/tmp'); os.makedirs(d, exist_ok=True)
        name = args[1] if len(args) > 1 else dt.datetime.now().strftime('%Y%m%d-%H%M%S')
        f = f'{d}/bri-{name}.log'
        print(f'+ recording → {f}\n  drive workflow in another shell with `a bri <cmd>` then Ctrl-C\n')
        os.execvp('sh', ['sh', '-c', f'tail -F -n 0 {LOG} | tee {f!r}']); return
    if a == 'deploy':  # zero-click rebuild+install of bri-ext + Firefox restart
        import subprocess, shutil, os, glob
        here = os.path.dirname(os.path.abspath(__file__))
        extdir = os.path.join(here, 'bri-ext')
        subprocess.check_call(['zip','-jq', f'{extdir}/a-bridge.xpi']
                              + [f for f in glob.glob(f'{extdir}/*') if not f.endswith('.xpi')])
        # Prefer Nightly profile (active one); fall back to dev/release.
        cands = glob.glob(os.path.expanduser('~/Library/Application Support/Firefox/Profiles/*')) \
              + glob.glob(os.path.expanduser('~/.mozilla/firefox/*default*'))
        prof = [p for p in cands if 'nightly' in p.lower()] or \
               [p for p in cands if p.endswith('default-dev')] or \
               [p for p in cands if p.endswith('default-release')] or cands
        if not prof: sys.stderr.write('x no FF profile found\n'); sys.exit(1)
        os.makedirs(f'{prof[0]}/extensions', exist_ok=True)
        shutil.copy(f'{extdir}/a-bridge.xpi', f'{prof[0]}/extensions/a-bridge@seanpatten.xpi')
        print(f'  → {prof[0]}/extensions/')
        _ff_restart()
        print(f'deployed bri-ext v{json.load(open(f"{extdir}/manifest.json"))["version"]}'); return
    if a.startswith('{'):
        msg = a
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
        # NOT recommended — prefer text/html/url. PNG is lossy for any text-bearing
        # page, heavy (200KB+), and requires a vision model to parse. Use only when
        # the rendered pixels themselves are the artifact (canvas, layout bug, etc).
        elif a=='screenshot':
            import base64, time as _t
            out = args[1] if len(args)>1 else f'/tmp/bri-{int(_t.time())}.png'
            s = socket.socket(); s.connect(('127.0.0.1', CMD))
            s.sendall(json.dumps({'id':int(time.time()*1000)%10**9,'action':'screenshot','to':to}).encode()+b'\n')
            buf=b''
            while True:
                ch=s.recv(1<<16)
                if not ch: break
                buf+=ch
            for line in buf.decode(errors='replace').splitlines():
                try: v=json.loads(line).get('value','')
                except Exception: continue
                if v.startswith('data:image/'):
                    open(out,'wb').write(base64.b64decode(v.split(',',1)[1]))
                    print(out); return
            sys.stderr.write('x no image returned (ext loaded? a bri deploy)\n'); sys.exit(1)
        else:
            sys.stderr.write("bri <url> | text [sel] | click <sel> | type <sel> <text> | keys <sel> <key> | url | deploy | restart | mon | '{json}'\n"
                "  deploy:  rebuild lib/bri-ext xpi + restart Firefox\n"
                "  restart: quit + relaunch Firefox Nightly (clears tab leaks)\n"
                "  mon:     bri.py + Firefox CPU/RAM/tabs snapshot\n"
                "  raw {json} supports all 9 actions + custom fields — use for "
                "eval/wait/html or any control the shortcuts hide.\n"); sys.exit(1)
        if to != 'all': j['to'] = to   # CLI targets firefox by default; @all (or BRI_TO=all) broadcasts to every browser
        msg = json.dumps(j)
    s = socket.socket()
    try: s.connect(('127.0.0.1', CMD))
    except OSError: sys.stderr.write("x bridge not running — start it: a bri\n"); sys.exit(1)
    s.sendall((msg+'\n').encode())
    while (ch := s.recv(1<<16)): sys.stdout.write(ch.decode(errors='replace'))

MENU = """a bri <cmd>     extension bridge to Firefox/Chrome (bri-ext / bri-chrome)
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
  new <id> [url]   open a job tab tagged #brijob=<id> (default chatgpt.com) for parallel jobs
  hint [match]     label clickable els on tabs whose URL contains <match> (host or brijob=<id>)
  hint-click <C>   click element C (re-enumerates; optional [match] filter, same as hint)
  save <url> [nt]  log a research/chat URL → urls.txt (a bri <N> reopens)
  screenshot [p]   PNG of active tab → p (default /tmp/bri-<ts>.png)
                   ! avoid if possible — prefer text/html/url (text is the
                     artifact, PNG is lossy + heavy + needs a vision model)
  '{json}'         raw passthrough — full 9-action protocol
first run — Firefox: a bri serve ff then a bri deploy   ·   Chrome: a bri serve then load bri-chrome (a extload)"""

if __name__=='__main__':
    args = sys.argv[1:]
    if args and args[0] == 'bri': args = args[1:]  # `a bri …` passes cmd name as argv[1]
    if not args:
        up = __import__('subprocess').run(['ss','-ltn','sport = :1234'],capture_output=True,text=True).stdout
        print(f"[{'running' if ':1234' in up else 'stopped'}] :1234")
        # Recent research URLs — logged generically via `a bri save <url>` (any site).
        # Numbered → `a bri <N>` opens URL N. URL on its own line so terminals
        # that auto-detect plain URLs make them clickable too.
        try:
            with open(__import__('os').path.expanduser('~/a/adata/git/urls.txt')) as f:
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
