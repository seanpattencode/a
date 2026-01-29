# Rename from `aio` to `a` - Migration Checklist

When setting up a new device or checking an existing one, verify these changes.

## 1. GitHub Repos

- **Code repo**: `seanpattencode/a` (was `aio`)
- **Sync repo**: `seanpattencode/a-sync` (was `aios-sync`)

## 2. Local Project Directory

```bash
# Should be at:
~/projects/a

# NOT at:
~/projects/aio
```

## 3. Symlink

```bash
# Check:
ls -la ~/.local/bin/a

# Should point to:
~/.local/bin/a -> ~/projects/a/a.py

# NOT to aio.py or /projects/aio/
```

## 4. Data Directory

```bash
# Should be at:
~/.local/share/a/

# NOT at:
~/.local/share/aios/

# If migrating, move it:
mv ~/.local/share/aios ~/.local/share/a
```

## 5. Data Sync Git Remote

```bash
cd ~/.local/share/a
git remote -v

# Should show:
# origin https://github.com/seanpattencode/a-sync (fetch)
# origin https://github.com/seanpattencode/a-sync (push)

# If wrong, fix with:
git remote set-url origin https://github.com/seanpattencode/a-sync
```

## 6. Shell Function in ~/.bashrc (and ~/.zshrc)

The `a()` function should reference `~/.local/share/a/` not `~/.local/share/aios/`:

```bash
a() {
    local cache=~/.local/share/a/help_cache.txt projects=~/.local/share/a/projects.txt icache=~/.local/share/a/i_cache.txt
    ...
    command python3 ~/.local/bin/a "$@"
}
```

Key paths to check in the function:
- `~/.local/share/a/help_cache.txt`
- `~/.local/share/a/projects.txt`
- `~/.local/share/a/i_cache.txt`
- `~/.local/share/a/timing.jsonl`
- `~/.local/bin/a`

## 7. Tmux Config Directory

```bash
# Should be at:
~/.a/tmux.conf

# NOT at:
~/.aios/tmux.conf

# Check ~/.tmux.conf contains:
source-file ~/.a/tmux.conf  # a
```

## 8. Worktrees Directory

```bash
# Default location changed to:
~/projects/aWorktrees

# From:
~/projects/aiosWorktrees
```

## 9. Project Files Renamed

| Old Name | New Name |
|----------|----------|
| aio.py | a.py |
| aio_cmd/ | a_cmd/ |
| aio-i | a-i |
| aio-fast | a-fast |

## 10. Quick Reinstall

If easier, just reinstall fresh:

```bash
# Clone fresh
cd ~/projects
git clone https://github.com/seanpattencode/a

# Run installer (updates shell functions, symlinks, etc)
cd a && ./install.sh

# Migrate data if needed
mv ~/.local/share/aios ~/.local/share/a
cd ~/.local/share/a
git remote set-url origin https://github.com/seanpattencode/a-sync
git pull
```

## 11. Database App Commands

Check for old paths in app commands:

```bash
sqlite3 ~/.local/share/a/aio.db "SELECT name, command FROM apps"

# Fix any ~/aio or ~/projects/aio references
# Also ensure python3 not python
```

## 12. Backward Compatibility

The `aio` command still works but shows a rename message:

```
aio has been renamed a. Yeah i know it sucks but its faster in the long term.
2.9x fewer errors on mobile, don't need English to type a. See ideas/LATENCY.md
```
