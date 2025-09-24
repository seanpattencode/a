#!/usr/bin/env python3
import argparse
import subprocess
import json
from pathlib import Path

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
services_file = aios_dir / "services.json"

services_file.touch(exist_ok=True)
services_default = {
    "backup": {
        "unit": "aios-backup.service",
        "exec": "/usr/bin/python3 /home/user/backup_local.py backup"
    },
    "scraper": {
        "unit": "aios-scraper.timer",
        "exec": "/usr/bin/python3 /home/user/web_scraper.py scrape"
    }
}

services = json.loads(services_file.read_text()) if services_file.read_text() else services_default
services_file.write_text(json.dumps(services, indent=2))

def start_service(name):
    service = services.get(name, {})
    unit = service.get("unit", f"aios-{name}.service")
    result = subprocess.run(["systemctl", "--user", "start", unit], capture_output=True)
    print(f"Started {name}" if result.returncode == 0 else f"Failed: {result.stderr.decode()}")

def stop_service(name):
    service = services.get(name, {})
    unit = service.get("unit", f"aios-{name}.service")
    result = subprocess.run(["systemctl", "--user", "stop", unit], capture_output=True)
    print(f"Stopped {name}" if result.returncode == 0 else f"Failed: {result.stderr.decode()}")

def service_status():
    for name, service in services.items():
        unit = service.get("unit", f"aios-{name}.service")
        result = subprocess.run(["systemctl", "--user", "is-active", unit], capture_output=True, text=True)
        status = result.stdout.strip()
        print(f"{name}: {status}")

def list_services():
    for name in services.keys():
        print(name)

parser = argparse.ArgumentParser(description="AIOS Service Manager")
subparsers = parser.add_subparsers(dest="command", help="Commands")

start_parser = subparsers.add_parser("start", help="Start service")
start_parser.add_argument("name", help="Service name")
stop_parser = subparsers.add_parser("stop", help="Stop service")
stop_parser.add_argument("name", help="Service name")
status_parser = subparsers.add_parser("status", help="Service status")
list_parser = subparsers.add_parser("list", help="List services")

args = parser.parse_args()

commands = {
    "start": lambda: start_service(args.name),
    "stop": lambda: stop_service(args.name),
    "status": service_status,
    "list": list_services
}

command_func = commands.get(args.command, list_services)
command_func()