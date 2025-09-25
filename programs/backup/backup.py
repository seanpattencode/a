#!/usr/bin/env python3
import sys
sys.path.append('/home/seanpatten/projects/AIOS')
import shutil
from pathlib import Path
import aios_db
from datetime import datetime

config = aios_db.read("backup") or {"source": str(Path.home()), "dest": "/tmp/backup"}
source = Path(config.get("source", Path.home()))
dest = Path(config.get("dest", "/tmp/backup")) / f"{datetime.now():%Y%m%d_%H%M%S}"

shutil.copytree(source, dest, dirs_exist_ok=True)
log = aios_db.read("backup_log") or []
aios_db.write("backup_log", log + [{"time": datetime.now().isoformat(), "dest": str(dest)}])
print(f"Backed up to {dest}")