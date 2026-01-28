"""aio gdrive - Cloud sync"""
import sys
from . _common import cloud_configured, cloud_login, cloud_logout, cloud_sync, cloud_pull_notes, cloud_status, _confirm

def run():
    wda = sys.argv[2] if len(sys.argv) > 2 else None
    if wda == 'login': cloud_login()
    elif wda == 'logout': cloud_logout()
    elif wda == 'sync': cloud_sync(wait=True)
    elif wda == 'pull': cloud_pull_notes()
    else: cloud_status()
