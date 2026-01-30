# Sync System Rewrite Summary

## Overview
Massive simplification of the sync system - removed complex event-sourcing in favor of simple git-based file sync.

## Changes Made

### Deleted (-3892 tokens)
- `sync.py` - old folder-based sync (47 lines)
- `_common.py` - removed `emit_event`, `replay_events`, `db_sync`, `auto_backup`, `EVENTS_PATH` (74 lines)
- `backup.py`, `note.py`, `rebuild.py` - replaced with placeholders
- All sync calls from: `hub.py`, `ssh.py`, `scan.py`, `add.py`, `remove.py`, `log.py`
- Timing logging from `a.py`

### New Sync System
- **Location**: `~/projects/a-sync/` (sibling of `a`, dynamic per device)
- **Format**: RFC 5322 `.txt` files (human readable, machine searchable, doesn't break)
- **Structure**: Multiple isolated repos to prevent conflicts

```
~/projects/a-sync/
  common/  → github.com/user/aio-common  (general files)
  ssh/     → github.com/user/aio-ssh     (SSH hosts)
  logs/    → github.com/user/aio-logs    (agent logs)
```

### Key Design Decisions
1. **Isolation** - sync conflict in notes doesn't bottleneck agent work logging
2. **Visibility** - a-sync added as project index 1, easy to browse/edit
3. **Dynamic paths** - works across devices regardless of install location
4. **RFC 5322 format** - doesn't hide information, doesn't break, machine searchable with metadata

### SSH Storage (RFC 5322)
```
Name: hostname
Host: user@ip:port
Password: base64_encoded
```

### Commands
- `a sync` - syncs all repos, shows status
- `a ssh` - loads from `a-sync/ssh/*.txt`
- `a 1` - opens a-sync folder directly

## Files Modified
- `a_cmd/sync.py` - new multi-repo sync system
- `a_cmd/ssh.py` - RFC 5322 file storage
- `a.py` - added sync command, removed timing
- `a_cmd/_common.py` - removed all sync logic
- `a_cmd/hub.py`, `log.py`, `scan.py`, `add.py`, `remove.py` - removed sync calls

## Status
- [x] Delete old sync logic
- [x] Create GitHub repo sync
- [x] Multi-repo isolation
- [x] SSH RFC 5322 storage
- [x] Dynamic paths
- [x] Add a-sync as project
- [ ] Notes migration
- [ ] Hub jobs migration
