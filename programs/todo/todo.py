#!/usr/bin/env python3
import sys
sys.path.append("/home/seanpatten/projects/AIOS/core")
sys.path.append('/home/seanpatten/projects/AIOS')
import aios_db
from datetime import datetime

tasks = aios_db.read("tasks") or []
command = sys.argv[1] if len(sys.argv) > 1 else "list"

actions = {
    "list": lambda: [print(f"{i+1}. {t}") for i, t in enumerate(tasks)],
    "add": lambda: aios_db.write("tasks", tasks + [f"[ ] {datetime.now():%Y-%m-%d %H:%M} {' '.join(sys.argv[2:])}"]),
    "done": lambda: aios_db.write("tasks", [t.replace("[ ]", "[x]") if i == int(sys.argv[2])-1 else t for i, t in enumerate(tasks)]),
    "clear": lambda: aios_db.write("tasks", [t for t in tasks if not t.startswith("[x]")])
}

actions.get(command, actions["list"])()