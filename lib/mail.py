#!/usr/bin/env python3
"""a mail <draft> — open a pre-filled compose window; YOU review and press Send.

Why a wrapper and not SMTP: spatten2@fordham.edu is Google Workspace behind
Fordham's SAML SSO (loginp.fordham.edu) — SSO accounts have no usable password
and institutional Workspace blocks app-passwords, so smtplib is a dead end there.
So we wrap a real client (Thunderbird) in COMPOSE mode: automation fills
to/subject/body, the human clicks Send. That structurally kills the "press wrong
button, send a half-finished email" failure — nothing auto-sends.

Draft file format (see i: idata/local/resume/roles/<slug>/email.md):
  To: addr
  Subject: ...
  Attach: file1.pdf, file2.docx      (optional; paths relative to the draft's dir)
  <blank line>
  ...body...

Attachments are copied to ~/Downloads with their clean names for one-drag attach
(mailto can't carry local attachments safely, by design).
  a mail <draft>     open compose for this draft
  a mail setup       one-time Thunderbird + Fordham setup steps
"""
import glob, os, pathlib, shutil, subprocess, sys, urllib.parse
args = sys.argv[1:]
if args and args[0] == 'mail': args = args[1:]   # `a mail …` passes cmd name as argv[1]

def tb_ready():
    for p in glob.glob(os.path.expanduser('~/.thunderbird/*/prefs.js')):
        try:
            if 'mail.account.' in open(p, encoding='utf-8', errors='ignore').read(): return True
        except OSError: pass
    return False

SETUP = """a mail setup — one-time Thunderbird + Fordham (Google Workspace / SAML SSO)
 1. thunderbird &                         # opens the account wizard
 2. Add spatten2@fordham.edu; Thunderbird detects Google -> "Sign in with Google"
 3. A browser opens -> Fordham SSO (creds + 2FA) -> allow Thunderbird
 4. Token persists; `a mail <draft>` opens a ready-to-send compose from then on.
Draft to send: a mail ~/i/idata/local/resume/roles/exa-infra/email.md"""

if not args or args[0] == 'setup':
    print("Thunderbird already has an account — `a mail <draft>` is ready." if tb_ready() else SETUP)
    sys.exit(0)

draft = pathlib.Path(args[0]).expanduser()
if not draft.exists(): sys.exit(f'x no draft file: {draft}')
hdr, body, in_body = {}, [], False
for ln in draft.read_text().splitlines():
    if in_body: body.append(ln); continue
    if not ln.strip(): in_body = True; continue
    k, _, v = ln.partition(':'); hdr[k.strip().lower()] = v.strip()
to, subject = hdr.get('to', ''), hdr.get('subject', '')
if not to: sys.exit('x draft has no "To:" line')
body_txt = '\n'.join(body).strip() + '\n'

dl = pathlib.Path.home() / 'Downloads'; dl.mkdir(exist_ok=True); staged = []
for a in [x.strip() for x in hdr.get('attach', '').split(',') if x.strip()]:
    src = (draft.parent / a).expanduser()
    if src.exists(): shutil.copy(src, dl / src.name); staged.append(dl / src.name)
    else: print(f'  ! attach not found, skipped: {src}')

mailto = 'mailto:' + urllib.parse.quote(to) + '?' + urllib.parse.urlencode(
    {'subject': subject, 'body': body_txt})
if not tb_ready():
    print('Thunderbird has no account yet — run `a mail setup` first.\n' + SETUP); sys.exit(1)
subprocess.Popen(['thunderbird', mailto], stdout=subprocess.DEVNULL,
                 stderr=subprocess.DEVNULL, start_new_session=True)
print(f'+ compose opened: To {to}')
print(f'  Subject: {subject}')
if staged:
    print('  attach from ~/Downloads (drag into the window):')
    for s in staged: print(f'    {s}')
print('  From must be spatten2@fordham.edu — then review and click Send.')
