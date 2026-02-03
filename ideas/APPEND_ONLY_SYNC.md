# Append-Only File Sync

## Status
Draft - not yet implemented

## Problem

Current sync causes conflicts when multiple devices edit and push without pulling first. Git rejects pushes when branches diverge.

## Failed Approaches

| Approach | Why It Failed |
|----------|---------------|
| `git pull --no-rebase` | Creates merge commits, still conflicts on same file edits |
| `git pull --rebase` | Works but requires manual intervention when conflicts occur |
| `git stash/pop` | Corrupts binary files (SQLite), complex error handling |
| Filesystem watchers | Editors do weird things (temp files, atomic saves), race conditions |
| Last-write-wins | Loses data silently |

The root cause: **git sees modified files as conflicts** when two devices edit the same file.

## Solution: Append-Only

Never modify files from git's perspective. Every "edit" creates a new file with a new timestamp. Git only sees additions = no conflicts possible.

```
OLD: edit fix-bug.txt in place → git sees modification → conflict
NEW: edit creates fix-bug_{new-timestamp}.txt → git sees addition → no conflict
```

## Filename Format

```
{name}_{timestamp}.{ext}

Timestamp: ISO 8601 Basic with nanosecond precision
           YYYYMMDDTHHMMss.nnnnnnnnn

Example:   fix-bug_20260203T084517.243881068.txt
Regex:     ^(.+)_(\d{8}T\d{6}\.\d{9})\.(\w+)$
```

### Why This Format

- **ISO 8601 standard** - not invented, widely understood
- **Nanosecond precision** - max available from Linux kernel, prevents collisions
- **Name in filename** - human readable when browsing folders
- **No device ID** - variable length complicates parsing, ns precision sufficient
- **Sortable** - alphabetical sort = chronological for same {name}

## Sync Rules

```
ON SYNC (pre-commit hook or script):

1. NEW FILE (untracked):
   - Add timestamp if missing
   - Stage

2. MODIFIED FILE:
   - Archive old version to .archive/{name}_{old_timestamp}.{ext}
   - Rename current to {name}_{new_timestamp}.{ext}
   - Stage both

3. DELETED FILE:
   - Stage removal (git history preserves; archiving preferred but not required)

4. THEN:
   - git commit -m "sync"
   - git pull --rebase
   - git push
```

## Invariants

- Main folder = current versions only
- `.archive/` = superseded versions
- Git only sees: new files, renames, deletions
- Modified content = archived old + new file (never in-place edit)
- Sorting alphabetically = chronological for same name

## Implementation

- Pre-sync script (~30-50 lines)
- NOT filesystem watcher (too buggy)
- Generate timestamp: `date +%Y%m%dT%H%M%S.%N`
- Parse timestamp: `grep -oE '[0-9]{8}T[0-9]{6}\.[0-9]{9}'`

## Migration

Current files lack timestamps. Options:
1. Add timestamps to all at once
2. Add as files are modified
3. Grandfather old files

Decision: TBD

## See Also

- `a_cmd/sync.py` - current implementation (has comment referencing this doc)
- `SYNC_ARCHITECTURE.md` - database sync via events.jsonl
- `temporary new sync.md` - earlier thinking on folder-as-source-of-truth
