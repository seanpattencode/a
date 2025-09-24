#!/usr/bin/env python3
import argparse
import shutil
import json
from pathlib import Path
from datetime import datetime, timedelta

aios_dir = Path.home() / ".aios"
backup_base = Path.home() / ".aios_backup"
aios_dir.mkdir(exist_ok=True)
backup_base.mkdir(exist_ok=True)

def get_backup_dir(date=None):
    date_str = date or datetime.now().strftime("%Y-%m-%d")
    return backup_base / date_str

def backup_command():
    today_backup = get_backup_dir()
    today_backup.mkdir(exist_ok=True)

    manifest = {}
    for file in aios_dir.rglob("*"):
        if not file.is_file():
            continue

        rel_path = file.relative_to(aios_dir)
        dest_file = today_backup / rel_path
        dest_file.parent.mkdir(parents=True, exist_ok=True)

        src_stat = file.stat()
        dest_exists = dest_file.exists()
        needs_copy = not dest_exists or src_stat.st_mtime > dest_file.stat().st_mtime

        if needs_copy:
            shutil.copy2(file, dest_file)

        manifest[str(rel_path)] = {
            "size": src_stat.st_size,
            "mtime": src_stat.st_mtime,
            "copied": needs_copy
        }

    manifest_file = today_backup / "manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2))

    cutoff = datetime.now() - timedelta(days=7)
    for old_backup in backup_base.iterdir():
        if old_backup.is_dir() and old_backup.name.count("-") == 2:
            backup_date = datetime.strptime(old_backup.name, "%Y-%m-%d")
            if backup_date < cutoff:
                shutil.rmtree(old_backup)

    print(f"Backup complete: {today_backup}")

def restore_command(date):
    backup_dir = get_backup_dir(date)

    if not backup_dir.exists():
        print(f"No backup for {date}")
        return

    for file in backup_dir.rglob("*"):
        if file.name == "manifest.json" or not file.is_file():
            continue

        rel_path = file.relative_to(backup_dir)
        dest_file = aios_dir / rel_path
        dest_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, dest_file)

    print(f"Restored from {date}")

def list_command():
    backups = sorted(backup_base.iterdir())
    for backup in backups:
        manifest_file = backup / "manifest.json"
        file_count = len(json.loads(manifest_file.read_text())) if manifest_file.exists() else 0
        print(f"{backup.name}: {file_count} files")

def clean_command():
    for backup in backup_base.iterdir():
        shutil.rmtree(backup)
    print("Cleaned all backups")

parser = argparse.ArgumentParser(description="AIOS Local Backup")
subparsers = parser.add_subparsers(dest="command", help="Commands")

backup_parser = subparsers.add_parser("backup", help="Create backup")
restore_parser = subparsers.add_parser("restore", help="Restore backup")
restore_parser.add_argument("date", help="Backup date (YYYY-MM-DD)")
list_parser = subparsers.add_parser("list", help="List backups")
clean_parser = subparsers.add_parser("clean", help="Clean all backups")

args = parser.parse_args()

commands = {
    "backup": backup_command,
    "restore": lambda: restore_command(args.date),
    "list": list_command,
    "clean": clean_command
}

command_func = commands.get(args.command, backup_command)
command_func()