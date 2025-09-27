#!/usr/bin/env python3
import json, sqlite3
from pathlib import Path
db_file = Path(__file__).parent.parent / "aios.db"
db_path = Path.home() / ".aios"
_db = sqlite3.connect(db_file, check_same_thread=False, isolation_level=None)
_db.execute("PRAGMA synchronous=OFF")
_db.execute("PRAGMA journal_mode=MEMORY")
_db.execute("CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT)")
def read(name):
    r = _db.execute("SELECT v FROM kv WHERE k=?", (name,)).fetchone()
    assert r, f"Missing: {name}"
    return json.loads(r[0])
def write(name, data):
    _db.execute("INSERT OR REPLACE INTO kv VALUES(?,?)", (name, json.dumps(data, indent=2)))
    return data
def query(db, sql, params=()):
    return _db.execute(sql, params).fetchall()
def execute(db, sql, params=()):
    _db.execute(sql, params)