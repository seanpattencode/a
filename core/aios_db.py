#!/usr/bin/env python3
import json, sqlite3
from pathlib import Path
db_file = Path(__file__).parent.parent / "aios.db"
db_path = Path.home() / ".aios"
_db = sqlite3.connect(db_file, check_same_thread=False, isolation_level=None)
_db.execute("PRAGMA synchronous=OFF")
_db.execute("PRAGMA journal_mode=MEMORY")
_db.executescript("CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT);CREATE TABLE IF NOT EXISTS jobs(id INTEGER PRIMARY KEY, name TEXT, status TEXT, output TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP);CREATE TABLE IF NOT EXISTS messages(id INTEGER PRIMARY KEY, content TEXT, timestamp TEXT, source TEXT, priority INTEGER DEFAULT 0);CREATE TABLE IF NOT EXISTS worktrees(id INTEGER PRIMARY KEY, repo TEXT, branch TEXT, path TEXT, job_id INTEGER, model TEXT, task TEXT, status TEXT, output TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP);CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY, target TEXT, data TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")
def read(name):
    r = _db.execute("SELECT v FROM kv WHERE k=?", (name,)).fetchone()
    return json.loads(r[0]) if r else None
def write(name, data):
    _db.execute("INSERT OR REPLACE INTO kv VALUES(?,?)", (name, json.dumps(data, indent=2)))
    return data
def query(db, sql, params=()):
    return _db.execute(sql, params).fetchall()
def execute(db, sql, params=()):
    _db.execute(sql, params)