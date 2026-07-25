#!/usr/bin/env python3
"""a mail — Gmail DRAFTS via API: the draft lands complete (to/subject/body/attachments)
in Gmail web where Sean lives; he reviews there and presses Send. Nothing auto-sends.

Why drafts and not SMTP: spatten2@fordham.edu is Workspace behind Fordham SAML SSO —
no password auth exists; and a compose-wrapper (Thunderbird) was rejected: Gmail web is
the mailbox that's actually used. OAuth (loopback, desktop client) with scopes
openid email gmail.compose; gmail.compose covers drafts.* (create/get/list). Stdlib only.

  a mail auth [email]   one-time OAuth for that account (browser consent; token stored)
  a mail who            which account(s) a mail is registered as — THE identity that drafts land in
  a mail <draft.md>     create the draft (From: header picks the account; else sole stored one)
  a mail drafts [email] list current drafts via API (machine check that drafts really entered)

Draft file: To:/Subject:/[Attach: f1,f2 rel to draft dir]/[From: email]/blank/body.
Tokens: adata/local/gmail-oauth.json {email:{refresh_token,client_id,client_secret}} 600.
Client: adata/local/gmail-client.json (Google 'installed' client json). Testing-mode apps
expire refresh tokens after 7 days -> rerun a mail auth (error says so).
"""
import base64, json, os, pathlib, socket, subprocess, sys, urllib.parse, urllib.request
ROOT = pathlib.Path(__file__).resolve().parent.parent
STORE, CLIENT = ROOT/'adata/local/gmail-oauth.json', ROOT/'adata/local/gmail-client.json'
SCOPES = 'openid email https://www.googleapis.com/auth/gmail.compose'
args = sys.argv[1:]
if args and args[0] == 'mail': args = args[1:]

def load(p, dflt): return json.loads(p.read_text()) if p.exists() else dflt
def save(p, d): p.parent.mkdir(parents=True, exist_ok=True); p.write_text(json.dumps(d, indent=1)); p.chmod(0o600)
def post(url, data, hdr=None):
    r = urllib.request.Request(url, urllib.parse.urlencode(data).encode() if isinstance(data, dict) else data, hdr or {})
    try: return json.load(urllib.request.urlopen(r))
    except urllib.error.HTTPError as e: sys.exit(f'x {url.split("/")[2]} {e.code}: {e.read().decode()[:300]}')
def client():
    c = load(CLIENT, None) or sys.exit(f'x no OAuth client at {CLIENT} — put a Google "installed" client json there')
    c = c.get('installed', c); return c['client_id'], c['client_secret']

def access_token(email):
    st = load(STORE, {}); a = st.get(email) or sys.exit(f'x {email} not authorized — run: a mail auth {email}')
    body = {'client_id': a['client_id'], 'client_secret': a['client_secret'],
            'refresh_token': a['refresh_token'], 'grant_type': 'refresh_token'}
    r = urllib.request.Request('https://oauth2.googleapis.com/token', urllib.parse.urlencode(body).encode())
    try: return json.load(urllib.request.urlopen(r))['access_token']
    except urllib.error.HTTPError as e:
        if b'invalid_grant' in e.read(): sys.exit(f'x {email}: refresh token dead (7-day testing-mode expiry?) — rerun: a mail auth {email}')
        raise
def api(email, path, body=None, method=None):
    tok = access_token(email)
    u = f'https://gmail.googleapis.com/gmail/v1/users/me/{path}'
    r = urllib.request.Request(u, json.dumps(body).encode() if body else None,
        {'Authorization': 'Bearer ' + tok, 'Content-Type': 'application/json'}, method=method)
    try: return json.load(urllib.request.urlopen(r))
    except urllib.error.HTTPError as e: sys.exit(f'x gmail {e.code} {path}: {e.read().decode()[:300]}')

def auth(email):
    cid, csec = client()
    s = socket.socket(); s.bind(('127.0.0.1', 0)); s.listen(1); port = s.getsockname()[1]
    red = f'http://localhost:{port}'
    q = {'client_id': cid, 'redirect_uri': red, 'response_type': 'code', 'scope': SCOPES,
         'access_type': 'offline', 'prompt': 'consent'}
    if email: q['login_hint'] = email
    url = 'https://accounts.google.com/o/oauth2/v2/auth?' + urllib.parse.urlencode(q)
    print(f'consent URL (opening in firefox):\n  {url}\nwaiting on {red} …', flush=True)
    env = os.environ.copy(); env['MOZ_ENABLE_WAYLAND'] = '1'
    import glob
    socks = [os.path.basename(w) for w in sorted(glob.glob(f'/run/user/{os.getuid()}/wayland-*')) if not w.endswith('.lock')]
    if socks: env['WAYLAND_DISPLAY'] = socks[-1]
    try: subprocess.Popen(['firefox-nightly', '--new-tab', url], env=env, stdout=-3, stderr=-3, start_new_session=True)
    except FileNotFoundError: print('  (no firefox-nightly — open the URL yourself)')
    c, _ = s.accept(); req = c.recv(8192).decode(errors='replace')
    c.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h2>a mail: done, close this tab</h2>'); c.close(); s.close()
    code = urllib.parse.parse_qs(urllib.parse.urlparse(req.split(' ')[1]).query).get('code', [None])[0]
    if not code: sys.exit('x no code in callback (denied?)')
    t = post('https://oauth2.googleapis.com/token', {'code': code, 'client_id': cid, 'client_secret': csec,
             'redirect_uri': red, 'grant_type': 'authorization_code'})
    info = json.load(urllib.request.urlopen(urllib.request.Request(
        'https://openidconnect.googleapis.com/v1/userinfo', headers={'Authorization': 'Bearer ' + t['access_token']})))
    got = info.get('email', email or '?')
    st = load(STORE, {}); st[got] = {'refresh_token': t['refresh_token'], 'client_id': cid, 'client_secret': csec}
    save(STORE, st); print(f'+ authorized: {got}  (drafts will land in THIS mailbox)')
    if email and got.lower() != email.lower(): print(f'  ! you asked for {email} but Google signed in {got} — rerun and pick the right account')

def who():
    st = load(STORE, {})
    if not st: sys.exit('no accounts authorized — a mail auth <email>')
    for e in st:
        try:
            tok = access_token(e)
            info = json.load(urllib.request.urlopen(urllib.request.Request(
                'https://openidconnect.googleapis.com/v1/userinfo', headers={'Authorization': 'Bearer ' + tok})))
            print(f'{e}  [live ok — registered as {info.get("email", "?")}]')
        except SystemExit as x: print(f'{e}  [{x}]')

def parse(p):
    hdr, body, ib = {}, [], False
    for ln in p.read_text().splitlines():
        if ib: body.append(ln)
        elif not ln.strip(): ib = True
        else: k, _, v2 = ln.partition(':'); hdr[k.strip().lower()] = v2.strip()
    return hdr, '\n'.join(body).strip() + '\n'

def draft(p):
    from email.message import EmailMessage
    hdr, body = parse(p)
    to = hdr.get('to') or sys.exit('x draft has no To:')
    st = load(STORE, {})
    frm = hdr.get('from') or (list(st)[0] if len(st) == 1 else None) \
        or sys.exit(f'x several accounts ({", ".join(st)}) — add "From: <email>" to the draft')
    m = EmailMessage(); m['To'], m['Subject'], m['From'] = to, hdr.get('subject', ''), frm
    m.set_content(body)
    for a in [x.strip() for x in hdr.get('attach', '').split(',') if x.strip()]:
        f = (p.parent / a).expanduser()
        if not f.exists(): print(f'  ! attach missing, skipped: {f}'); continue
        ext = f.suffix.lower()
        mt, sub = {'.pdf': ('application', 'pdf'), '.docx': ('application', 'vnd.openxmlformats-officedocument.wordprocessingml.document')}.get(ext, ('application', 'octet-stream'))
        m.add_attachment(f.read_bytes(), maintype=mt, subtype=sub, filename=f.name)
    raw = base64.urlsafe_b64encode(m.as_bytes()).decode()
    d = api(frm, 'drafts', {'message': {'raw': raw}})
    got = api(frm, f'drafts/{d["id"]}?format=metadata')
    hs = {h['name'].lower(): h['value'] for h in got['message']['payload'].get('headers', [])}
    print(f'+ DRAFT CREATED in {frm}\n  id: {d["id"]}\n  api-verified subject: {hs.get("subject", "?")}')
    print(f'  review+send: https://mail.google.com/mail/?authuser={urllib.parse.quote(frm)}#drafts')

if not args: sys.exit(__doc__.strip())
if args[0] == 'auth': auth(args[1] if len(args) > 1 else ''); sys.exit(0)
if args[0] == 'who': who(); sys.exit(0)
if args[0] == 'drafts':
    e = args[1] if len(args) > 1 else (list(load(STORE, {})) or [sys.exit('x no accounts')])[0]
    for d in api(e, 'drafts?maxResults=10').get('drafts', []):
        g = api(e, f'drafts/{d["id"]}?format=metadata')
        hs = {h['name'].lower(): h['value'] for h in g['message']['payload'].get('headers', [])}
        print(f'{d["id"]}  {hs.get("subject", "(no subject)")[:70]}')
    sys.exit(0)
f = pathlib.Path(args[0]).expanduser()
if f.exists(): draft(f)
else: sys.exit(f'x unknown arg / no file: {args[0]}\n\n' + __doc__.strip())
