#!/bin/bash
# migrate_to_new_device.sh - Backup/restore logs and data between devices
# Usage: ./migrate_to_new_device.sh [backup|restore|pull-gdrive]

set -e

DATA_DIR="$HOME/.local/share/a"
LOGS_DIR="$DATA_DIR/logs"
BACKUP_DIR="/tmp/a-migration-$(date +%Y%m%d)"

backup() {
    echo "=== Backing up from this device ==="
    mkdir -p "$BACKUP_DIR"

    # Logs (most important - agent session history)
    if [ -d "$LOGS_DIR" ]; then
        echo "Copying logs..."
        cp -r "$LOGS_DIR" "$BACKUP_DIR/"
        echo "✓ Logs: $(du -sh "$LOGS_DIR" | cut -f1)"
    fi

    # Events (source of truth for sync)
    if [ -f "$DATA_DIR/events.jsonl" ]; then
        cp "$DATA_DIR/events.jsonl" "$BACKUP_DIR/"
        echo "✓ events.jsonl: $(du -sh "$DATA_DIR/events.jsonl" | cut -f1)"
    fi

    # Auth files
    mkdir -p "$BACKUP_DIR/auth"
    [ -f ~/.config/gh/hosts.yml ] && cp ~/.config/gh/hosts.yml "$BACKUP_DIR/auth/"
    [ -f ~/.config/rclone/rclone.conf ] && cp ~/.config/rclone/rclone.conf "$BACKUP_DIR/auth/"
    echo "✓ Auth files copied"

    # Database (local cache, can be rebuilt but nice to have)
    if [ -f "$DATA_DIR/aio.db" ]; then
        cp "$DATA_DIR/aio.db" "$BACKUP_DIR/"
        echo "✓ aio.db: $(du -sh "$DATA_DIR/aio.db" | cut -f1)"
    fi

    # Create tarball
    TARBALL="$HOME/a-backup-$(hostname)-$(date +%Y%m%d).tar.gz"
    tar -czf "$TARBALL" -C "$(dirname "$BACKUP_DIR")" "$(basename "$BACKUP_DIR")"
    rm -rf "$BACKUP_DIR"

    echo ""
    echo "=== Backup complete ==="
    echo "File: $TARBALL"
    echo "Size: $(du -sh "$TARBALL" | cut -f1)"
    echo ""
    echo "Transfer to new device:"
    echo "  scp $TARBALL newdevice:~/"
    echo ""
    echo "Then on new device:"
    echo "  ./migrate_to_new_device.sh restore ~/$(basename "$TARBALL")"
}

restore() {
    TARBALL="$1"
    if [ -z "$TARBALL" ] || [ ! -f "$TARBALL" ]; then
        echo "Usage: $0 restore <tarball>"
        echo "Example: $0 restore ~/a-backup-olddevice-20260129.tar.gz"
        exit 1
    fi

    echo "=== Restoring from backup ==="

    # Extract
    EXTRACT_DIR="/tmp/a-restore-$$"
    mkdir -p "$EXTRACT_DIR"
    tar -xzf "$TARBALL" -C "$EXTRACT_DIR"
    SRC=$(find "$EXTRACT_DIR" -maxdepth 1 -type d -name "a-migration-*" | head -1)

    if [ -z "$SRC" ]; then
        echo "x Invalid backup format"
        exit 1
    fi

    # Ensure data dir exists
    mkdir -p "$DATA_DIR"
    mkdir -p "$LOGS_DIR"

    # Restore logs (merge, don't overwrite)
    if [ -d "$SRC/logs" ]; then
        echo "Merging logs..."
        cp -n "$SRC/logs/"* "$LOGS_DIR/" 2>/dev/null || true
        echo "✓ Logs restored"
    fi

    # Restore events.jsonl (merge lines)
    if [ -f "$SRC/events.jsonl" ]; then
        if [ -f "$DATA_DIR/events.jsonl" ]; then
            echo "Merging events.jsonl..."
            cat "$SRC/events.jsonl" >> "$DATA_DIR/events.jsonl"
            sort -u "$DATA_DIR/events.jsonl" -o "$DATA_DIR/events.jsonl"
        else
            cp "$SRC/events.jsonl" "$DATA_DIR/"
        fi
        echo "✓ events.jsonl restored"
    fi

    # Restore auth (only if not present)
    if [ -f "$SRC/auth/hosts.yml" ] && [ ! -f ~/.config/gh/hosts.yml ]; then
        mkdir -p ~/.config/gh
        cp "$SRC/auth/hosts.yml" ~/.config/gh/
        echo "✓ gh auth restored"
    fi
    if [ -f "$SRC/auth/rclone.conf" ] && [ ! -f ~/.config/rclone/rclone.conf ]; then
        mkdir -p ~/.config/rclone
        cp "$SRC/auth/rclone.conf" ~/.config/rclone/
        echo "✓ rclone auth restored"
    fi

    # Restore database (only if not present)
    if [ -f "$SRC/aio.db" ] && [ ! -f "$DATA_DIR/aio.db" ]; then
        cp "$SRC/aio.db" "$DATA_DIR/"
        echo "✓ aio.db restored"
    fi

    rm -rf "$EXTRACT_DIR"

    echo ""
    echo "=== Restore complete ==="
    echo "Run 'a' to verify everything works"
}

pull_gdrive() {
    echo "=== Pulling from Google Drive ==="

    if ! command -v rclone &>/dev/null; then
        echo "x rclone not installed. Run: a gdrive login"
        exit 1
    fi

    REMOTE=$(rclone listremotes | grep -E '^aio-gdrive' | head -1 | tr -d ':')
    if [ -z "$REMOTE" ]; then
        echo "x No gdrive remote configured. Run: a gdrive login"
        exit 1
    fi

    echo "Using remote: $REMOTE"

    # Pull logs from all devices
    echo "Pulling logs..."
    mkdir -p "$LOGS_DIR"
    rclone copy "$REMOTE:aio-backup/logs/" "$LOGS_DIR/" --progress

    # Pull events.jsonl
    echo "Pulling events.jsonl..."
    rclone copy "$REMOTE:aio-backup/events.jsonl" "$DATA_DIR/" --progress

    # Pull auth (careful - don't overwrite existing)
    echo "Checking auth files..."
    if [ ! -f ~/.config/gh/hosts.yml ]; then
        mkdir -p ~/.config/gh
        rclone copy "$REMOTE:aio-backup/auth/hosts.yml" ~/.config/gh/ 2>/dev/null && echo "✓ gh auth pulled" || true
    fi
    if [ ! -f ~/.config/rclone/rclone.conf ]; then
        # Can't pull rclone.conf with rclone (chicken-egg), but worth noting
        echo "Note: rclone.conf must be set up manually (a gdrive login)"
    fi

    echo ""
    echo "=== Pull complete ==="
    echo "Run 'a' to verify, then 'a gdrive sync' to push any local changes"
}

push_db() {
    echo "=== Pushing DB backups to Google Drive ==="

    if ! command -v rclone &>/dev/null; then
        echo "x rclone not installed. Run: a gdrive login"
        exit 1
    fi

    # Get all configured remotes
    REMOTES=$(rclone listremotes | grep -E '^aio-gdrive' | tr -d ':')
    if [ -z "$REMOTES" ]; then
        echo "x No gdrive remote configured. Run: a gdrive login"
        exit 1
    fi

    DATE=$(date +%Y%m%d)
    HOSTNAME=$(hostname)
    MIGRATION_FOLDER="migration/${HOSTNAME}-${DATE}"

    # Collect db files
    DB_FILES=$(ls "$DATA_DIR"/aio*.db 2>/dev/null)
    if [ -z "$DB_FILES" ]; then
        echo "x No database files found in $DATA_DIR"
        exit 1
    fi

    echo "Found database files:"
    ls -lh "$DATA_DIR"/aio*.db

    # Create temp dir with just the db files
    TMP_DIR="/tmp/a-db-backup-$$"
    mkdir -p "$TMP_DIR"
    cp "$DATA_DIR"/aio*.db "$TMP_DIR/"

    # Also include events.jsonl and current logs
    [ -f "$DATA_DIR/events.jsonl" ] && cp "$DATA_DIR/events.jsonl" "$TMP_DIR/"
    [ -d "$LOGS_DIR" ] && cp -r "$LOGS_DIR" "$TMP_DIR/"

    echo ""
    echo "Uploading to: aio-backup/$MIGRATION_FOLDER"
    echo "Total size: $(du -sh "$TMP_DIR" | cut -f1)"
    echo ""

    # Upload to each configured remote
    for REMOTE in $REMOTES; do
        echo ">>> Uploading to $REMOTE..."
        rclone copy "$TMP_DIR" "$REMOTE:aio-backup/$MIGRATION_FOLDER" --progress
        if [ $? -eq 0 ]; then
            echo "✓ $REMOTE complete"
        else
            echo "x $REMOTE failed"
        fi
        echo ""
    done

    rm -rf "$TMP_DIR"

    echo "=== Push complete ==="
    echo "Uploaded to: aio-backup/$MIGRATION_FOLDER"
    echo ""
    echo "To restore on new device:"
    echo "  rclone copy aio-gdrive:aio-backup/$MIGRATION_FOLDER ~/.local/share/a/"
}

case "${1:-backup}" in
    backup)
        backup
        ;;
    restore)
        restore "$2"
        ;;
    pull-gdrive|pull)
        pull_gdrive
        ;;
    push-db|push)
        push_db
        ;;
    *)
        echo "Usage: $0 [backup|restore <tarball>|pull-gdrive|push-db]"
        echo ""
        echo "Commands:"
        echo "  backup       Create tarball of logs, events, auth, db"
        echo "  restore      Restore from tarball (merges, doesn't overwrite)"
        echo "  pull-gdrive  Pull logs and events from Google Drive"
        echo "  push-db      Push db backups to both gdrive accounts"
        exit 1
        ;;
esac
