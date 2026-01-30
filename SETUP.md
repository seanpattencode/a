# Setup a on new device via SSH

```bash
# 1. Clone repo
ssh user@host 'git clone https://github.com/seanpattencode/aio.git ~/projects/a'

# 2. Create symlink
ssh user@host 'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'

# 3. Update shell (adds PATH to bashrc/zshrc)
ssh user@host 'cd ~/projects/a && python a.py update shell'

# 4. Sync all repos (ssh, notes, login, workspace, etc)
ssh user@host 'cd ~/projects/a && python a.py sync'

# 5. Apply gh token from sync (if not logged in)
ssh user@host 'cd ~/projects/a && python a.py login apply'
```

Or via `a ssh`:
```bash
a ssh <host> 'git clone https://github.com/seanpattencode/aio.git ~/projects/a'
a ssh <host> 'mkdir -p ~/.local/bin && ln -sf ~/projects/a/a.py ~/.local/bin/a'
a ssh <host> 'cd ~/projects/a && python a.py update shell && python a.py sync && python a.py login apply'
```

After setup, `a <#>` auto-clones missing projects using synced gh token.
