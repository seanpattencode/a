#!/usr/bin/env python3
# experimental — promoted from my/auto/ after working end-to-end on Gemini + Claude.ai.
"""bri — userscript bridge via HTTP long-poll (bypasses page CSP, works on
Gemini/Claude.ai/other strict-CSP LLM UIs). Listens :1234 (HTTP: serves
userscript, GET /poll for next cmd, POST /resp for results); :1235 (push JSON).

Two interchangeable page-side clients (pick one or run both — server doesn't care):
  (a) Tampermonkey userscript — Setup: install Tampermonkey extension → open
      http://127.0.0.1:1234/a.user.js → click Install/Update. Per-bump cost is
      one Update click. Works on any browser that has TM.
  (b) Firefox WebExtension at lib/bri-ext/ — Setup: drop a-bridge.xpi into
      <profile>/extensions/ + set user_pref("xpinstall.signatures.required",
      false) in user.js + restart FF. No per-bump click; rebuild xpi & restart.
      Wins over userscript: eval works on chatgpt.com (TM hits CSP there);
      fetch goes through extension context (no GM_xhr). Same dispatch loop,
      same /poll + /resp protocol — server is identical for both clients.
  Both: launch Firefox WITHOUT -marionette → sign into target site once.

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

Chat-UI recipe (proven on Gemini and Claude.ai):
  # Gemini
  a bri https://gemini.google.com/app
  a bri click 'rich-textarea [contenteditable]'             # focus
  a bri type  'rich-textarea [contenteditable]' 'your prompt'
  a bri keys  'rich-textarea [contenteditable]' Enter       # submit
  sleep 8; a bri text 'message-content'                     # read reply
  # Claude.ai (same pattern, different selectors)
  a bri https://claude.ai/new
  a bri click 'div[contenteditable=true]'
  a bri type  'div[contenteditable=true]' 'your prompt'
  a bri keys  'div[contenteditable=true]' Enter
  sleep 8; a bri text '.font-claude-response'

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
- Each command gets one response per connected frame (main + iframes). For
  body text the main frame's value is the longest; iframes return null/empty.
- {action:"eval"} is CSP-blocked on Google domains and Claude.ai — use
  text/click/type/keys/html instead; those go through DOM APIs, no eval.
Tail full event stream: tail -f /tmp/bri.log
"""
import socket, threading, queue, json, sys, time

PORT, CMD, LOG = 1234, 1235, '/tmp/bri.log'
pollers, pending = [], {}  # pollers: list[Queue]  pending: id -> Queue

USERSCRIPT = r"""// ==UserScript==
// @name         a-bridge
// @namespace    https://github.com/seanpattencode/a
// @version      0.7
// @match        *://*/*
// @grant        GM_xmlhttpRequest
// @connect      127.0.0.1
// @run-at       document-end
// @updateURL    http://127.0.0.1:1234/a.user.js
// @downloadURL  http://127.0.0.1:1234/a.user.js
// ==/UserScript==
// HTTP long-poll via GM_xmlhttpRequest — runs in TM's privileged extension
// context, bypasses page CSP for our own network. WebSocket from page context
// (v0.2) was blocked by strict connect-src on Gemini/Claude.ai. GM_xhr is fine.
// Dep: Tampermonkey extension. Alternative client with same protocol but
// fewer dep clicks: a-bridge WebExtension at lib/bri-ext/ (drop xpi).
// Why TM beats Marionette/CDP for signed-in automation:
//   Marionette: Firefox hard-codes navigator.webdriver=true while -marionette
//     runs. Google → "browser may not be secure". Restart FF without it.
//   Chrome CDP: 136+ blocks --remote-debugging-port on default profile;
//     attaching enables Runtime/Page with detectable side effects.
//   TM + GM_xhr: no driver attached, no page-CSP filter. Real DOM, real user.
(() => {
  const POLL = 'http://127.0.0.1:1234/poll', RESP = 'http://127.0.0.1:1234/resp';
  const $ = s => document.querySelector(s);
  const dispatch = async (m) => {
    try {
      switch (m.action) {
        case 'navigate': location.href = m.url; return {ok:true};
        case 'click':    $(m.sel).click(); return {ok:true};
        case 'type':     { let e=$(m.sel);
                           if (!e.isContentEditable && e.tagName==='DIV')
                             e = e.querySelector('[contenteditable]') || e;
                           e.focus();
                           if (e.isContentEditable) document.execCommand('insertText',false,m.text);
                           else e.value = m.text;
                           e.dispatchEvent(new InputEvent('input',{bubbles:true,data:m.text,inputType:'insertText'}));
                           return {ok:true}; }
        case 'keys':     { const e=$(m.sel)||document.activeElement; e.focus();
                           (Array.isArray(m.keys)?m.keys:[m.keys]).forEach(k=>{
                             ['keydown','keyup'].forEach(t=>e.dispatchEvent(new KeyboardEvent(t,{key:k,bubbles:true,cancelable:true}))); });
                           return {ok:true}; }
        case 'text':     return {ok:true, value:$(m.sel).innerText};
        case 'html':     return {ok:true, value:document.documentElement.outerHTML.slice(0,200000)};
        case 'find':     { // walk open shadow roots; match by visible text OR aria-label (case-insens, contains)
                           const need = (m.text||'').toLowerCase().trim();
                           const sel = m.sel || 'button, [role="button"], a, [tabindex]:not([tabindex="-1"])';
                           const hits = [];
                           const walk = root => {
                             for (const el of root.querySelectorAll(sel)) {
                               const t = ((el.innerText||el.textContent||'')+' '+(el.getAttribute('aria-label')||'')).toLowerCase();
                               if (!need || t.includes(need)) hits.push(el);
                             }
                             for (const el of root.querySelectorAll('*')) if (el.shadowRoot) walk(el.shadowRoot);
                           };
                           walk(document);
                           if (m.click && hits.length) hits[0].click();
                           return {ok:true, value:{n:hits.length, first:(hits[0]?(hits[0].innerText||hits[0].getAttribute('aria-label')||'').trim().slice(0,80):null)}}; }
        case 'eval':     { let c=m.code; try{if(window.trustedTypes&&trustedTypes.createPolicy){const tt=window._abp||(window._abp=trustedTypes.createPolicy('abridge',{createScript:s=>s}));c=tt.createScript(m.code);}}catch(e){}
                           return {ok:true, value:await (async()=>eval(c))()}; }
        case 'wait':     await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
        case 'url':      return {ok:true, value:location.href};
        default: return {error:'unknown action: '+m.action};
      }
    } catch (e) { return {error:String(e)}; }
  };
  const post = d => GM_xmlhttpRequest({url:RESP, method:'POST',
    headers:{'Content-Type':'application/json'}, data:JSON.stringify({src:location.href, ...d})});
  post({hello:location.href, title:document.title});
  const loop = () => GM_xmlhttpRequest({
    url:POLL, method:'GET', timeout:30000,
    onload: async r => {
      if (r.status === 200 && r.responseText) {
        try { const c=JSON.parse(r.responseText); post({id:c.id, ...await dispatch(c)}); }
        catch (e) { post({error:String(e)}); }
      }
      loop();
    },
    onerror: () => setTimeout(loop, 2000),
    ontimeout: () => loop(),
  });
  loop();
})();
"""

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
        q = queue.Queue(); pollers.append(q)
        try: cmd = q.get(timeout=25)
        except Exception: cmd = None
        try: pollers.remove(q)
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
            rid = json.loads(m).get('id')
            if rid in pending: pending[rid].put(m)
        except Exception: pass
        http_send(c, '200 OK'); c.close(); return
    if method == 'GET' and '.user.js' in path:
        http_send(c, '200 OK', USERSCRIPT.encode(), 'application/javascript; charset=utf-8')
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
        rid = None
        try: rid = json.loads(msg).get('id')
        except Exception: pass
        if rid is not None: pending[rid] = queue.Queue()
        for q in list(pollers): q.put(msg)
        if rid is None:
            c.send(f'sent to {len(pollers)} pollers\n'.encode())
        else:
            out, end = [], time.time()+8
            while time.time() < end:
                try: out.append(pending[rid].get(timeout=end-time.time()))
                except Exception: break
            del pending[rid]
            c.send(('\n'.join(out) or '{"error":"no response"}').encode()+b'\n')
        c.close()

def main():
    threading.Thread(target=cmd_serve, daemon=True).start()
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    s.bind(('127.0.0.1',PORT)); s.listen(50); log(f'[*] http on :{PORT} | log: {LOG}')
    while True:
        c,addr = s.accept()
        threading.Thread(target=handle, args=(c,addr), daemon=True).start()

# Shortcut CLI: thin client that pushes JSON to a running bridge on :1235 and
# prints the response. The raw '{json}' form remains the full API.
def _ff_restart():
    import subprocess
    subprocess.run(['osascript','-e','tell application "Firefox Nightly" to quit'], stderr=subprocess.DEVNULL)
    for _ in range(15):
        if subprocess.run(['pgrep','-f','Firefox Nightly.app/Contents/MacOS/firefox$'], capture_output=True).returncode != 0: break
        time.sleep(1)
    time.sleep(0.5)
    subprocess.Popen(['/Applications/Firefox Nightly.app/Contents/MacOS/firefox','-foreground'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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

def client(args):
    a = args[0]
    if a == 'restart': _ff_restart(); print('restarted Firefox Nightly'); return
    if a == 'mon':     _mon(); return
    if a == 'deploy':  # zero-click rebuild+install of bri-ext + Firefox restart
        import subprocess, shutil, os, glob
        here = os.path.dirname(os.path.abspath(__file__))
        extdir = os.path.join(here, 'bri-ext')
        subprocess.check_call(['zip','-jq', f'{extdir}/a-bridge.xpi',
                               f'{extdir}/manifest.json', f'{extdir}/content.js'])
        # Prefer Nightly profile (active one); fall back to dev/release.
        cands = glob.glob(os.path.expanduser('~/Library/Application Support/Firefox/Profiles/*'))
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
        if a.startswith(('http://','https://')): j = {'action':'navigate','url':a}  # no id: page unloads
        elif a=='text':  j = {'id':1,'action':'text','sel':args[1] if len(args)>1 else 'body'}
        elif a=='click': j = {'id':1,'action':'click','sel':args[1]}
        elif a=='type':  j = {'id':1,'action':'type','sel':args[1],'text':args[2]}
        elif a=='keys':  j = {'id':1,'action':'keys','sel':args[1],'keys':args[2]}
        elif a=='url':   j = {'id':1,'action':'url'}
        else:
            sys.stderr.write("bri <url> | text [sel] | click <sel> | type <sel> <text> | keys <sel> <key> | url | deploy | restart | mon | '{json}'\n"
                "  deploy:  rebuild lib/bri-ext xpi + restart Firefox\n"
                "  restart: quit + relaunch Firefox Nightly (clears tab leaks)\n"
                "  mon:     bri.py + Firefox CPU/RAM/tabs snapshot\n"
                "  raw {json} supports all 9 actions + custom fields — use for "
                "eval/wait/html or any control the shortcuts hide.\n"); sys.exit(1)
        msg = json.dumps(j)
    s = socket.socket()
    try: s.connect(('127.0.0.1', CMD))
    except OSError: sys.stderr.write("x bridge not running — start it: a bri\n"); sys.exit(1)
    s.sendall((msg+'\n').encode()); sys.stdout.write(s.recv(1<<20).decode())

if __name__=='__main__':
    args = sys.argv[1:]
    if args and args[0] == 'bri': args = args[1:]  # `a bri …` passes cmd name as argv[1]
    (client(args) if args else main())
