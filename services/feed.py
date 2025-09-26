#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
sys.path.append('/home/seanpatten/projects/AIOS')
import aios_db
from datetime import datetime

command = sys.argv[1] if len(sys.argv) > 1 else "list"

aios_db.execute("feed", "CREATE TABLE IF NOT EXISTS messages(id INTEGER PRIMARY KEY, content TEXT, timestamp TEXT, source TEXT, priority INTEGER DEFAULT 0)")

def cmd_add():
    aios_db.execute("feed", "INSERT INTO messages(content, timestamp, source) VALUES (?, ?, ?)",
                   (" ".join(sys.argv[2:]), datetime.now().isoformat(), "manual"))

def print_message(row):
    is_today = datetime.fromisoformat(row[2]).date() == datetime.now().date()
    time_part = row[2].split('T')[1][:5]
    date_part = row[2].split('T')[0]
    print(is_today and f"{time_part} {row[1]}" or f"{date_part} {time_part} {row[1]}")

def cmd_list():
    messages = aios_db.query("feed", "SELECT id, content, timestamp, source FROM messages ORDER BY timestamp DESC LIMIT 50")
    list(map(print_message, messages))

def cmd_view():
    settings = aios_db.read('settings') or {}
    time_fmt = settings.get('time_format', '12h') == '12h' and '%I:%M %p' or '%H:%M'
    messages = aios_db.query("feed", "SELECT id, content, timestamp, source FROM messages ORDER BY timestamp DESC LIMIT 50")
    def print_view_message(row):
        is_today = datetime.fromisoformat(row[2]).date() == datetime.now().date()
        time_str = datetime.fromisoformat(row[2]).strftime(time_fmt)
        date_str = row[2].split('T')[0]
        print(is_today and f"{time_str} {row[1]}" or f"{date_str} {time_str} {row[1]}")
    list(map(print_view_message, messages))

def cmd_clear():
    aios_db.execute("feed", "DELETE FROM messages WHERE timestamp < datetime('now', '-7 days')")

actions = {
    "add": cmd_add,
    "list": cmd_list,
    "view": cmd_view,
    "clear": cmd_clear
}

actions.get(command, actions["list"])()