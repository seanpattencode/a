#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
sys.path.append('/home/seanpatten/projects/AIOS')
import subprocess
import aios_db

services = aios_db.read("services") or {}
command = sys.argv[1] if len(sys.argv) > 1 else "list"
name = sys.argv[2] if len(sys.argv) > 2 else None

actions = {
    "list": lambda: [print(f"{k}: {v.get('status', 'unknown')}") for k, v in services.items()],
    "start": lambda: aios_db.write("services", {**services, name: {**services.get(name, {}), "status": "running"}}),
    "stop": lambda: aios_db.write("services", {**services, name: {**services.get(name, {}), "status": "stopped"}}),
    "status": lambda: print(services.get(name, {}).get("status", "unknown") if name else "specify service")
}

actions.get(command, actions["list"])()