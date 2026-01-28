"""aio sync - central sync API

Usage:
    from aio_cmd.sync import put, get, delete, list_all, sync

    put("ssh", "server1", {"host": "user@1.2.3.4"})
    get("ssh", "server1")
    list_all("ssh")
    sync()

Structure:
    sync/api.py      - Central API (dispatches to backends)
    sync/backends/   - Implementations (git, crsqlite, http, etc.)
"""
from .api import put, get, delete, list_all, sync
