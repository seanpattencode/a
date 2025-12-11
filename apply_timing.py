import re
import os
import sys

with open('aio.py', 'r') as f:
    content = f.read()

# Add imports
if 'import io' not in content:
    content = content.replace('import sqlite3', 'import sqlite3\nimport io')

if 'import atexit' not in content:
    content = content.replace('import time', 'import time\nimport atexit')

timing_block_new = r'''
_START = time.time()
class OutputCapture:
    def __init__(self): self.t=sys.stdout; self.b=io.StringIO()
    def write(self,m): self.t.write(m); self.b.write(m)
    def flush(self): self.t.flush()
    def get(self): return self.b.getvalue()

_cap = OutputCapture()
sys.stdout = _cap

def _save_log():
    try:
        d=os.path.expanduser("~/.local/share/aios"); os.makedirs(d,exist_ok=True)
        with sqlite3.connect(os.path.join(d,"aio.db")) as c:
            c.execute("PRAGMA journal_mode=WAL;")
            c.execute("CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY, command TEXT, duration REAL, output TEXT, timestamp TEXT DEFAULT CURRENT_TIMESTAMP)")
            c.execute("INSERT INTO history (command, duration, output) VALUES (?,?,?)", (' '.join(sys.argv), time.time()-_START, _cap.get()))
    except: pass

atexit.register(_save_log)

_orig_execvp = os.execvp
def _timed_execvp(file, args):
    _save_log()
    return _orig_execvp(file, args)
os.execvp = _timed_execvp
'''

# Replace old timing block
pattern = re.compile(r'_START = time\.time\(\).*?atexit\.register\(_save_timing\)', re.DOTALL)

if pattern.search(content):
    content = pattern.sub(timing_block_new, content)
    print("Replaced timing block")
else:
    # Insert after imports
    insert_pt = content.find("from pathlib import Path")
    if insert_pt != -1:
        insert_pt = content.find("\n", insert_pt) + 1
        content = content[:insert_pt] + "\n" + timing_block_new + "\n" + content[insert_pt:]
        print("Inserted timing block")
    else:
        print("Could not find insertion point")

with open('aio.py', 'w') as f:
    f.write(content)