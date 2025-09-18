#!/usr/bin/env python3
import sys
import os
import json
import threading
import time
import sqlite3
from pathlib import Path

# Add the googleDriveSyncDemo folder to path
sys.path.insert(0, str(Path(__file__).parent / 'googleDriveSyncDemo'))

# Database path
DB_PATH = Path(__file__).parent.parent / 'orchestrator.db'
WEB_SERVER_THREAD = None
WEB_SERVER_STARTED = False

def get_config(key):
    """Get a config value from the database"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.execute("SELECT value FROM config WHERE key = ?", (key,))
    result = cursor.fetchone()
    conn.close()
    return result["value"] if result else None

def set_config(key, value):
    """Set a config value in the database"""
    conn = sqlite3.connect(DB_PATH)
    conn.execute(
        "INSERT OR REPLACE INTO config (key, value, updated) VALUES (?, ?, ?)",
        (key, value, time.time())
    )
    conn.commit()
    conn.close()

def check_auth():
    """Check if we have valid authentication in database"""
    token_data = get_config('google_drive_token')
    if token_data:
        try:
            token = json.loads(token_data)
            if 'token' in token or 'refresh_token' in token:
                return True
        except:
            pass
    return False

def start_web_server():
    """Start the Flask web server in a thread"""
    global WEB_SERVER_STARTED
    try:
        # Change to the googleDriveSyncDemo directory
        import os
        original_dir = os.getcwd()
        os.chdir(Path(__file__).parent / 'googleDriveSyncDemo')

        from app import app
        print("\nStarting authentication web server at http://localhost:5000")
        app.run(host='0.0.0.0', port=5000, debug=False)
        WEB_SERVER_STARTED = True
    except Exception as e:
        print(f"Failed to start web server: {e}")

def backup_to_drive(*args, **kwargs):
    """Main backup function called by orchestrator"""
    if check_auth():
        try:
            # Import necessary modules
            from datetime import datetime
            from pathlib import Path

            # Change to googleDriveSyncDemo directory to use the app functions
            import os
            original_dir = os.getcwd()
            os.chdir(Path(__file__).parent / 'googleDriveSyncDemo')

            # Import the app functions
            from google.oauth2.credentials import Credentials
            from googleapiclient.discovery import build
            from googleapiclient.http import MediaFileUpload

            # Load credentials from database
            token_data = get_config('google_drive_token')
            if not token_data:
                os.chdir(original_dir)
                return "No token found in database"

            token_json = json.loads(token_data)
            creds = Credentials.from_authorized_user_info(token_json)
            service = build('drive', 'v3', credentials=creds)

            # Ensure AIOS_Backups folder exists
            folder_name = 'AIOS_Backups'
            query = f"name='{folder_name}' and mimeType='application/vnd.google-apps.folder' and trashed=false"
            files = service.files().list(q=query, fields="files(id)", pageSize=1).execute().get('files', [])

            if files:
                folder_id = files[0]['id']
            else:
                folder_metadata = {'name': folder_name, 'mimeType': 'application/vnd.google-apps.folder'}
                folder = service.files().create(body=folder_metadata, fields='id').execute()
                folder_id = folder['id']
                print(f"Created {folder_name} folder in Google Drive")

            # Upload orchestrator.db with timestamp
            db_path = '../../orchestrator.db'
            if os.path.exists(db_path):
                timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
                file_metadata = {'name': f'orchestrator_{timestamp}.db', 'parents': [folder_id]}
                media = MediaFileUpload(db_path, mimetype='application/x-sqlite3')
                file = service.files().create(body=file_metadata, media_body=media, fields='id,name').execute()

                os.chdir(original_dir)
                print(f"Database backed up successfully: {file.get('name')}")
                return f"Backup completed: {file.get('name')}"
            else:
                os.chdir(original_dir)
                print("Database file not found")
                return "Database file not found"

        except Exception as e:
            print(f"Backup error: {e}")
            return f"Backup failed: {e}"
    else:
        print("Google Drive backup: Not authenticated")
        return "Authentication required"

def init_check():
    """Called at orchestrator startup to check auth and start web server if needed"""
    global WEB_SERVER_THREAD

    if not check_auth():
        print("\n" + "="*60)
        print("GOOGLE DRIVE BACKUP - AUTHENTICATION REQUIRED AT http://localhost:5000")
        print("="*60)
        print("Starting web server for authentication...")
        print("1. Open http://localhost:5000 in your browser")
        print("2. Click 'Sign in with Google'")
        print("3. Authorize the application")
        print("="*60 + "\n")

        # Start web server in background thread
        WEB_SERVER_THREAD = threading.Thread(target=start_web_server, daemon=True)
        WEB_SERVER_THREAD.start()

        # Give server time to start
        time.sleep(2)
        return False
    else:
        print("\n" + "="*60)
        print("GOOGLE DRIVE BACKUP - AUTHENTICATION SUCCESSFUL")
        print("="*60)
        print("Backup service is ready and will run every 2 hours")
        print("="*60 + "\n")
        return True

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "check":
        init_check()
        # Keep script running if web server is started
        if WEB_SERVER_THREAD and WEB_SERVER_THREAD.is_alive():
            WEB_SERVER_THREAD.join()
    else:
        backup_to_drive()