"""aio sync - Append-only file sync (folder = source of truth, zero conflicts)"""
import os, sqlite3, subprocess as sp, time, hashlib
from pathlib import Path

SYNC_DIR = Path(os.path.expanduser('~/gdrive/a'))
NOTES, HUB = SYNC_DIR/'notes', SYNC_DIR/'hub'
DB = Path(os.path.expanduser('~/.local/share/a/aio.db'))
_git = lambda *a: sp.run(['git','-C',str(SYNC_DIR)]+list(a), capture_output=True, text=True)

def init():
    NOTES.mkdir(parents=True, exist_ok=True); HUB.mkdir(parents=True, exist_ok=True)
    (SYNC_DIR/'.git').exists() or _git('init','-b','main')

def pull():
    """Pull if behind, rebuild cache"""
    if _git('fetch','-q').returncode==0 and 'behind' in _git('status','-uno').stdout:
        _git('pull','--ff-only','-q'); rebuild(); return True
    return False

def push(msg='sync'):
    _git('add','-A'); _git('commit','-qm',msg); _git('push','-q')

def rebuild():
    """Rebuild DB cache from files"""
    c = sqlite3.connect(DB); c.execute('DROP TABLE IF EXISTS notes2'); c.execute('CREATE TABLE notes2(id PRIMARY KEY,t,s DEFAULT 0,ts)')
    notes = {}
    for f in sorted(NOTES.glob('*')):
        parts = f.stem.split('_',1); ts = int(parts[0]) if parts[0].isdigit() else 0; nid = parts[1] if len(parts)>1 else parts[0]
        if f.suffix == '.md': notes[nid] = (f.read_text().strip(), 0, ts)
        elif f.suffix == '.ack' and nid in notes: notes[nid] = (notes[nid][0], 1, ts)
    for nid,(txt,s,ts) in notes.items(): c.execute('INSERT INTO notes2 VALUES(?,?,?,?)',(nid,txt,s,ts))
    c.execute('DROP TABLE IF EXISTS notes'); c.execute('ALTER TABLE notes2 RENAME TO notes'); c.commit(); c.close()

def note_add(txt):
    ts, nid = int(time.time()), hashlib.md5(f'{time.time()}{os.getpid()}'.encode()).hexdigest()[:8]
    (NOTES/f'{ts}_{nid}.md').write_text(txt)
    c = sqlite3.connect(DB); c.execute('INSERT OR REPLACE INTO notes VALUES(?,?,0,?)',(nid,txt,ts)); c.commit(); c.close()
    push(f'n:{txt[:20]}'); return nid

def note_ack(nid):
    (NOTES/f'{int(time.time())}_{nid}.ack').touch()
    c = sqlite3.connect(DB); c.execute('UPDATE notes SET s=1 WHERE id=?',(nid,)); c.commit(); c.close()
    push(f'ack:{nid}')

def note_list(active=True):
    c = sqlite3.connect(DB); r = c.execute(f'SELECT id,t,ts FROM notes WHERE s={"0" if active else "0 OR 1"} ORDER BY ts DESC').fetchall(); c.close(); return r
