#!/usr/bin/env python3
# experimental — promoted from my/auto/ after working end-to-end on Gemini + Claude.ai.
"""bri — userscript bridge via HTTP long-poll (bypasses page CSP, works on
Gemini/Claude.ai/other strict-CSP LLM UIs). Listens :1234 (HTTP: serves
userscript, GET /poll for next cmd, POST /resp for results); :1235 (push JSON).

Setup once: install Tampermonkey in Firefox → run this → open
http://127.0.0.1:1234/a.user.js → click Install (or Update if already
installed at older version) → launch Firefox WITHOUT -marionette → sign
into target site manually once.

Drive shortcuts (bridge must already be running in another process):
  bri <url>          navigate (http/https prefix detected)
  bri text [sel]     read body or selected element (sel defaults to "body")
  bri click <sel>    click element
  bri type <sel> <s> type into element (handles contenteditable like rich-textarea)
  bri keys <sel> <k> dispatch keydown/keyup (e.g. "Enter" to submit)
  bri url            return current URL
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
// @version      0.4
// @match        *://*/*
// @grant        GM_xmlhttpRequest
// @connect      127.0.0.1
// @run-at       document-end
// ==/UserScript==
// HTTP long-poll via GM_xmlhttpRequest — runs in TM's privileged extension
// context, bypasses page CSP. WebSocket from page context (v0.2) was blocked
// by strict connect-src on Gemini/Claude.ai. GM_xhr is unaffected.
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
        case 'eval':     return {ok:true, value:await (async()=>eval(m.code))()};
        case 'wait':     await new Promise(r=>setTimeout(r,m.ms||500)); return {ok:true};
        case 'url':      return {ok:true, value:location.href};
        default: return {error:'unknown action: '+m.action};
      }
    } catch (e) { return {error:String(e)}; }
  };
  const post = d => GM_xmlhttpRequest({url:RESP, method:'POST',
    headers:{'Content-Type':'application/json'}, data:JSON.stringify(d)});
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
            out, end = [], time.time()+3
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
def client(args):
    a = args[0]
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
            sys.stderr.write("bri <url> | text [sel] | click <sel> | type <sel> <text> | keys <sel> <key> | url | '{json}'\n"
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
