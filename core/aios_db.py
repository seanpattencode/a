#!/usr/bin/env python3
import json
from pathlib import Path
import sqlite3

db_path = Path.home() / ".aios"
db_path.mkdir(exist_ok=True)

def read(name):
    file = db_path / f"{name}.json"
    assert file.exists(), f"Missing: {file}"
    return json.loads(file.read_text())

def write(name, data):
    (db_path / f"{name}.json").write_text(json.dumps(data, indent=2))
    return data

def query(db, sql, params=()):
    conn = sqlite3.connect(db_path / f"{db}.db")
    result = conn.execute(sql, params).fetchall()
    conn.commit()
    conn.close()
    return result

def execute(db, sql, params=()):
    conn = sqlite3.connect(db_path / f"{db}.db")
    conn.execute(sql, params)
    conn.commit()
    conn.close()