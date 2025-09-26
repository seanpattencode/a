#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
sys.path.append('/home/seanpatten/projects/AIOS')
import subprocess
import aios_db

services = aios_db.read("services") or {}
command = sys.argv[1] if len(sys.argv) > 1 else "list"
name = sys.argv[2] if len(sys.argv) > 2 else None

def cmd_list():
    list(map(print, [f"{k}: {v.get('status', 'unknown')}" for k, v in services.items()]))

def cmd_start():
    aios_db.write("services", {**services, name: {**services.get(name, {}), "status": "running"}})

def cmd_stop():
    aios_db.write("services", {**services, name: {**services.get(name, {}), "status": "stopped"}})

def cmd_status():
    print(name and services.get(name, {}).get("status", "unknown") or "specify service")

{"list": cmd_list, "start": cmd_start, "stop": cmd_stop, "status": cmd_status}.get(command, cmd_list)()