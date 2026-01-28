# Sync System Status

## Completed

- **Log sync to GDrive** - logs now sync to `aio-backup/logs/{DEVICE_ID}/`
- **Multi-GDrive accounts** - 2 slots (`aio-gdrive`, `aio-gdrive2`) for redundancy
- **Auth sync** - `~/.config/gh/hosts.yml` and `~/.config/rclone/rclone.conf` backed up to `auth/`
- **Granular `aio backup`** - shows data sizes, sync targets, clickable URLs
- **Removed dead `notebook/`** - legacy code cleaned up
- **Migration script** - `feature_tests/migrate_logs.py` moves flat logs to `_old/`
- **Install.sh updated** - rclone added as dependency for all OS

## Current Remote Structure

```
aio-gdrive:aio-backup/
├── auth/
│   ├── hosts.yml      # gh token
│   └── rclone.conf    # rclone tokens
├── logs/
│   ├── Pixel-10-Pro/  # termux logs
│   ├── ubuntuSSD4Tb/  # local machine logs
│   └── _old/          # migrated flat files (backup)
└── events.jsonl       # source of truth
```

## Verified Working

| Device | Status |
|--------|--------|
| ubuntuSSD4Tb (local) | ✓ Full sync working |
| Pixel-10-Pro (termux) | ✓ Logs syncing to device folder |
| HSU (home server) | ⚠ Needs rclone setup |

## Remaining Tasks

### HSU Setup
```bash
# rclone installed to ~/.local/bin but needs verification
aio ssh 2 "rclone version"
aio ssh 2 "aio gdrive login"   # or aio gdrive init if auth exists
```

### New Device Onboarding
```bash
# 1. Install aio
./install.sh

# 2. Bootstrap gdrive (manual oauth once)
aio gdrive login

# 3. Pull existing auth tokens
aio gdrive init

# 4. Optionally add second gdrive account
aio gdrive login
```

### Optional Future Work
- Auto-cleanup of `logs/_old/` after 30 days
- Add rclone user-space install fallback in `cloud_install()` when no sudo
