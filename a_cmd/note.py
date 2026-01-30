"""aio note - Notes (RFC 5322 .txt storage)"""
import sys, subprocess as sp
from pathlib import Path
from datetime import datetime
from . _common import DEVICE_ID, SYNC_ROOT
from .sync import sync

NOTES_DIR = SYNC_ROOT / 'notes'
def _id(): return datetime.now().strftime('%Y%m%d%H%M%S')
def _save(nid, text, status='pending', project=None, due=None, device=None):
    NOTES_DIR.mkdir(parents=True, exist_ok=True)
    (NOTES_DIR/f'{nid}.txt').write_text(f"ID: {nid}\nText: {text}\nStatus: {status}\nDevice: {device or DEVICE_ID}\nCreated: {datetime.now():%Y-%m-%d %H:%M}\n"+(f"Project: {project}\n" if project else "")+(f"Due: {due}\n" if due else ""))
    sync('notes')
def _load():
    NOTES_DIR.mkdir(parents=True, exist_ok=True); (NOTES_DIR/'.git').exists() or sync('notes'); notes = []
    for f in NOTES_DIR.glob('*.txt'):
        d = {k.strip(): v.strip() for line in f.read_text().splitlines() if ':' in line for k, v in [line.split(':', 1)]}
        if 'ID' in d and 'Text' in d: notes.append((d['ID'], d['Text'], d.get('Due'), d.get('Project'), d.get('Device',DEVICE_ID), d.get('Status','pending'), d.get('Created')))
    return sorted(notes, key=lambda x: x[0], reverse=True)
def _rm(nid): (NOTES_DIR/f'{nid}.txt').unlink(missing_ok=True); sync('notes')

def run():
    raw = ' '.join(sys.argv[2:]) if len(sys.argv) > 2 else None
    notes = _load(); pending = [n for n in notes if n[5]=='pending']

    if raw and raw[0]!='?': _save(_id(), raw); print("✓"); return
    if raw: pending = [n for n in pending if raw[1:].lower() in n[1].lower()]

    if not pending: print("a n <text>"); return
    if not sys.stdin.isatty(): [print(f"{t}" + (f" @{p}" if p else "")) for _,t,_,p,_,_,_ in pending[:10]]; return

    url = sp.run(['git','-C',str(NOTES_DIR),'remote','get-url','origin'], capture_output=True, text=True).stdout.strip()
    print(f"Notes: {len(pending)} pending\n  {NOTES_DIR}\n  {url}\n")
    print(f"{len(pending)} notes | [a]ck [e]dit [s]earch [q]uit | 1/20=due")
    i = 0
    while i < len(pending):
        nid,txt,due,proj,dev,_,_ = pending[i]
        print(f"\n[{i+1}/{len(pending)}] {txt}" + (f" @{proj}" if proj else "") + (f" [{due}]" if due else "") + (f" <{dev[:8]}>" if dev else "")); ch = input("> ").strip()
        if ch == 'a': old = pending[i]; _save(old[0], old[1], 'done', old[3], old[2], old[4]); print("✓"); pending.pop(i); continue
        elif ch == 'e': nv = input("new: ").strip(); nv and (_save(nid, nv, 'pending', proj, due, dev), print("✓")); pending = _load(); pending = [n for n in pending if n[5]=='pending']; continue
        elif '/' in ch:
            from dateutil.parser import parse
            d = str(parse(ch, dayfirst=False))[:10]
            _save(nid, txt, 'pending', proj, d, dev); print(f"✓ {d}"); pending = _load(); pending = [n for n in pending if n[5]=='pending']; continue
        elif ch == 's': q = input("search: "); pending = [n for n in _load() if n[5]=='pending' and q.lower() in n[1].lower()]; i=0; print(f"{len(pending)} results"); continue
        elif ch == 'q': return
        elif ch: _save(_id(), ch); pending.insert(0, (_id(),ch,None,None,DEVICE_ID,'pending',None)); print(f"✓ [{len(pending)}]"); continue
        i += 1
