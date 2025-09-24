#!/usr/bin/env python3
import argparse
import json
import pickle
from pathlib import Path
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
from google.auth.transport.requests import Request
from google_auth_oauthlib.flow import InstalledAppFlow

aios_dir = Path.home() / ".aios"
aios_dir.mkdir(exist_ok=True)
token_file = aios_dir / "gdrive_token.pickle"
creds_file = aios_dir / "gdrive_credentials.json"
sync_state_file = aios_dir / "gdrive_sync.json"

SCOPES = ["https://www.googleapis.com/auth/drive.file"]

def get_credentials():
    creds = None

    if token_file.exists():
        with open(token_file, "rb") as token:
            creds = pickle.load(token)

    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(str(creds_file), SCOPES)
            creds = flow.run_local_server(port=0)

        with open(token_file, "wb") as token:
            pickle.dump(creds, token)

    return creds

def sync_to_drive():
    creds = get_credentials()
    service = build("drive", "v3", credentials=creds)

    sync_state = json.loads(sync_state_file.read_text()) if sync_state_file.exists() else {}

    results = service.files().list(q="name='aios_backup' and mimeType='application/vnd.google-apps.folder'").execute()
    folders = results.get("files", [])

    folder_id = folders[0]["id"] if folders else None
    if not folder_id:
        folder_metadata = {"name": "aios_backup", "mimeType": "application/vnd.google-apps.folder"}
        folder = service.files().create(body=folder_metadata, fields="id").execute()
        folder_id = folder.get("id")

    for file_path in aios_dir.rglob("*"):
        if not file_path.is_file():
            continue

        rel_path = str(file_path.relative_to(aios_dir))
        file_stat = file_path.stat()
        current_mtime = file_stat.st_mtime

        needs_upload = rel_path not in sync_state or sync_state[rel_path] < current_mtime

        if needs_upload:
            media = MediaFileUpload(str(file_path))
            file_metadata = {"name": rel_path, "parents": [folder_id]}

            existing = service.files().list(q=f"name='{rel_path}' and '{folder_id}' in parents").execute()
            existing_files = existing.get("files", [])

            if existing_files:
                service.files().update(fileId=existing_files[0]["id"], media_body=media).execute()
            else:
                service.files().create(body=file_metadata, media_body=media).execute()

            sync_state[rel_path] = current_mtime
            print(f"Uploaded: {rel_path}")

    sync_state_file.write_text(json.dumps(sync_state, indent=2))
    print("Sync complete")

def restore_from_drive():
    creds = get_credentials()
    service = build("drive", "v3", credentials=creds)

    results = service.files().list(q="name='aios_backup' and mimeType='application/vnd.google-apps.folder'").execute()
    folders = results.get("files", [])

    if not folders:
        print("No backup found")
        return

    folder_id = folders[0]["id"]
    files = service.files().list(q=f"'{folder_id}' in parents").execute().get("files", [])

    for file_info in files:
        file_id = file_info["id"]
        file_name = file_info["name"]
        local_path = aios_dir / file_name
        local_path.parent.mkdir(parents=True, exist_ok=True)

        request = service.files().get_media(fileId=file_id)
        content = request.execute()
        local_path.write_bytes(content)
        print(f"Restored: {file_name}")

def list_backups():
    sync_state = json.loads(sync_state_file.read_text()) if sync_state_file.exists() else {}
    for file_path in sync_state.keys():
        print(file_path)

def status_command():
    sync_state = json.loads(sync_state_file.read_text()) if sync_state_file.exists() else {}
    print(f"Files synced: {len(sync_state)}")
    print(f"Credentials: {'Configured' if creds_file.exists() else 'Not configured'}")

parser = argparse.ArgumentParser(description="AIOS Google Drive Backup")
subparsers = parser.add_subparsers(dest="command", help="Commands")

sync_parser = subparsers.add_parser("sync", help="Sync to Drive")
restore_parser = subparsers.add_parser("restore", help="Restore from Drive")
list_parser = subparsers.add_parser("list", help="List synced files")
status_parser = subparsers.add_parser("status", help="Show status")

args = parser.parse_args()

commands = {
    "sync": sync_to_drive,
    "restore": restore_from_drive,
    "list": list_backups,
    "status": status_command
}

command_func = commands.get(args.command, status_command)
command_func()