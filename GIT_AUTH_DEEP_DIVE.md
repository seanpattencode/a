# AIOS Git Authentication Deep Dive

## Overview

AIOS handles git authentication in two distinct contexts:
1. **Auto-Update Context** (aios.py): Automatic background updates
2. **Manual Git Operations** (aio.py): Push, pull, setup, revert commands

---

## 1. AUTO-UPDATE AUTHENTICATION (aios.py)

### Problem Statement

The legacy aios.py implements automatic background updates that:
- Run in background threads (non-blocking)
- Cannot prompt user for credentials
- May fail without interactive auth
- Need to handle SSH and HTTPS transparently

### Solution: Non-Interactive Git Environment

#### Step 1: Environment Variable Isolation

```python
git_env = os.environ.copy()
git_env['GIT_TERMINAL_PROMPT'] = '0'      # Disable all terminal prompts
git_env['GIT_ASKPASS'] = 'echo'           # Disable password prompts
git_env['SSH_ASKPASS'] = 'echo'           # Disable SSH password prompts
git_env['GIT_SSH_COMMAND'] = 'ssh -oBatchMode=yes'  # Disable SSH interactive auth
```

**What each variable does:**

| Variable | Effect | Rationale |
|----------|--------|-----------|
| `GIT_TERMINAL_PROMPT=0` | Disables terminal prompts | Prevents hanging on auth |
| `GIT_ASKPASS=echo` | Dummy credential provider | Returns empty instead of hanging |
| `SSH_ASKPASS=echo` | Dummy SSH password provider | Returns empty instead of hanging |
| `GIT_SSH_COMMAND='ssh -oBatchMode=yes'` | Batch mode (no interactive) | Prevents SSH key prompts |

#### Step 2: Stdin Prevention

All git operations include stdin redirection:

```python
result = sp.run([
    'git', '-C', str(install_dir), 'fetch', 'origin'
],
capture_output=True,
text=True,
stdin=subprocess.DEVNULL,  # Critical: prevents stdin reads
env=git_env,
timeout=10)
```

**Why this matters:**
- Without `DEVNULL`, git might try to read stdin (hang forever)
- With `DEVNULL`, git immediately fails if auth needed
- Allows HTTPS fallback to trigger

#### Step 3: Timeout Protection

All operations have explicit timeouts:

```python
timeout=5      # For quick operations (rev-parse, remote get-url)
timeout=10     # For fetch operations
timeout=15     # For pull operations
timeout=30     # For pip install
```

If git hangs, timeout expires and code continues to fallback logic.

### SSH to HTTPS Automatic Fallback

#### The Problem

SSH requires:
- Pre-configured SSH keys (~/.ssh/id_rsa)
- SSH agent running
- No passphrase (or ssh-agent unlocked)
- Network access to SSH port (22)

In background threads, any of these can fail silently.

#### The Solution

**Detection:**
```python
result = sp.run(['git', '-C', SCRIPT_DIR, 'remote', 'get-url', 'origin'],
              capture_output=True, text=True)
remote_url = result.stdout.strip()
is_ssh = remote_url.startswith('git@') or remote_url.startswith('ssh://')
```

**Conversion Logic:**

```python
# SSH URL formats
# Format 1: git@github.com:user/repo.git
# Format 2: ssh://git@github.com/user/repo.git

if result.returncode != 0 and is_ssh and auto:
    https_url = remote_url
    
    # Handle Format 1: git@host:user/repo
    if '@' in https_url and ':' in https_url:
        parts = https_url.split('@')[1].split(':')
        if len(parts) == 2:
            https_url = f"https://{parts[0]}/{parts[1]}"
    
    # Handle Format 2: ssh://git@host/user/repo
    https_url = https_url.replace('ssh://git@', 'https://')
```

**Conversion Examples:**

| SSH Format | HTTPS Result |
|-----------|--------------|
| `git@github.com:user/repo.git` | `https://github.com/user/repo.git` |
| `git@gitlab.com:org/project.git` | `https://gitlab.com/org/project.git` |
| `ssh://git@bitbucket.org/user/repo` | `https://bitbucket.org/user/repo` |
| `git@custom.git:path/to/repo` | `https://custom.git/path/to/repo` |

**Permanent Switch:**

```python
# Update remote permanently
sp.run(['git', '-C', str(install_dir), 'remote', 'set-url', 'origin', https_url],
       capture_output=True, stdin=subprocess.DEVNULL, env=git_env, timeout=5)

# Retry fetch with HTTPS
result = sp.run(['git', '-C', str(install_dir), 'fetch', 'origin'],
              capture_output=True, text=True, stdin=subprocess.DEVNULL,
              env=git_env, timeout=10)

if result.returncode == 0:
    # Success - configure credential cache for HTTPS
    sp.run(['git', '-C', str(install_dir), 'config', 'credential.helper', 
           'cache --timeout=604800'],
           capture_output=True, stdin=subprocess.DEVNULL, env=git_env, timeout=5)
```

### Credential Caching for HTTPS

#### Git Credential Helper Cache

```bash
git config credential.helper 'cache --timeout=604800'
```

**How it works:**

1. **First HTTPS operation:**
   - Git detects authentication needed
   - Prompts user for username/password (one-time)
   - Caches credentials in memory

2. **Subsequent operations (within 1 week):**
   - Git retrieves credentials from cache
   - No prompt needed
   - Non-blocking

3. **After timeout (1 week):**
   - Cache expires
   - Next operation prompts again

**Timeout Calculation:**
```
604800 seconds = 604800 / 3600 / 24 = 7 days
```

**Characteristics:**
- Memory-only storage (survives process restarts, but not system reboot)
- Per-host credentials (github.com, gitlab.com stored separately)
- Requires initial user interaction for first operation
- Silent failure if no credentials provided

### Credential Helper Flow

```
First HTTPS Operation:
  git fetch origin
    ↓
  Credential helper invoked
    ↓
  [User prompted for password]
    ↓
  Credentials cached (1 week)
    ↓
  Operation succeeds

Subsequent Operations (same repo):
  git pull origin
    ↓
  Credential helper found cached creds
    ↓
  [No prompt]
    ↓
  Operation succeeds
```

---

## 2. MANUAL GIT OPERATIONS (aio.py)

### Push Operation (Lines 2042-2180)

#### Basic Push (non-worktree)

```python
cwd = os.getcwd()

# Stage all changes
sp.run(['git', '-C', cwd, 'add', '-A'])

# Commit
result = sp.run(['git', '-C', cwd, 'commit', '-m', commit_msg],
               capture_output=True, text=True)

# Push
result = sp.run(['git', '-C', cwd, 'push'], capture_output=True, text=True)
```

#### Worktree Push (w++ command)

```python
# 1. Commit in worktree
sp.run(['git', '-C', worktree_path, 'add', '-A'])
sp.run(['git', '-C', worktree_path, 'commit', '-m', commit_msg])

# 2. Switch to main branch
sp.run(['git', '-C', project_path, 'checkout', main_branch])

# 3. Merge worktree branch
sp.run(['git', '-C', project_path, 'merge', worktree_branch, '--no-edit'])

# 4. Push to main
result = sp.run(['git', '-C', project_path, 'push', 'origin', main_branch],
               capture_output=True, text=True)
```

#### Authentication in Push

Current aio.py relies on:
- Git's configured credentials
- Pre-existing SSH keys or HTTPS cache
- User's git config

**No special handling** - assumes credentials are already set up.

### Pull Operation (Lines 2180-2201)

#### Destructive Reset

```python
cwd = os.getcwd()

# Display warning
print("⚠ WARNING: This will DELETE all local changes!")

# Get user confirmation (skip with -y flag)
response = input("Are you sure? (y/n): ").strip().lower()

# Fetch latest from remote
sp.run(['git', '-C', cwd, 'fetch', 'origin'], capture_output=True)

# Hard reset to origin/main (or origin/master)
result = sp.run(['git', '-C', cwd, 'reset', '--hard', 'origin/main'],
               capture_output=True, text=True)

# Clean untracked files
sp.run(['git', '-C', cwd, 'clean', '-f', '-d'], capture_output=True)

print("✓ Local changes removed. Synced with server.")
```

#### No Special Authentication

- Uses git's default authentication
- Relies on pre-configured remotes
- No HTTPS fallback logic
- Will fail if credentials not available

### Setup Operation (Lines 2233-2338)

#### Repository Initialization

```python
# Check if already a repo
result = sp.run(['git', '-C', cwd, 'rev-parse', '--git-dir'], capture_output=True)
if result.returncode == 0:
    print("ℹ Already a git repository")
else:
    sp.run(['git', '-C', cwd, 'init'], capture_output=True)
    print("✓ Initialized git repository")

# Add remote
sp.run(['git', '-C', cwd, 'remote', 'add', 'origin', remote_url])

# Create initial commit
sp.run(['git', '-C', cwd, 'add', '-A'])
sp.run(['git', '-C', cwd, 'commit', '-m', 'Initial commit'])

# Set main branch and push
sp.run(['git', '-C', cwd, 'branch', '-M', 'main'])
result = sp.run(['git', '-C', cwd, 'push', '-u', 'origin', 'main'],
               capture_output=True, text=True)
```

#### GitHub CLI Integration

If `gh` CLI available:
```python
result = sp.run(['gh', 'repo', 'create', repo_name, visibility, 
                '--source=.', '--push'],
              capture_output=True, text=True)
```

Uses GitHub's authentication (pre-configured).

---

## 3. AUTHENTICATION COMPARISON

### Auto-Update (aios.py) vs Manual (aio.py)

| Aspect | Auto-Update | Manual |
|--------|------------|--------|
| **Non-interactive** | Yes (must be) | No (can prompt) |
| **SSH Fallback** | Yes (HTTPS auto-switch) | No (assumes configured) |
| **Credential Caching** | Configured for HTTPS | Relies on git config |
| **Timeout Protection** | Yes (5-30s) | No timeouts |
| **Stdin Prevention** | Yes (DEVNULL) | No DEVNULL |
| **Error Handling** | Silent failure | Shows error messages |
| **User Intervention** | Never required | May require credentials |

### When Each is Used

**aios.py auto-update:**
- Runs in background thread
- Cannot show UI
- Must not hang
- Example: Daily automatic update check

**aio.py manual commands:**
- Runs in foreground
- User is present
- Can prompt if needed
- Example: User types `aio push "message"`

---

## 4. POTENTIAL SECURITY ISSUES

### Concerns in Current Implementation

1. **Plain Text in Logs**
   - Git errors show stderr (may contain URLs with credentials)
   - Mitigation: HTTPS cache doesn't store passwords on disk

2. **SSH Key Access**
   - Requires SSH keys without passphrases (or ssh-agent)
   - Mitigation: Auto-fallback to HTTPS removes SSH requirement

3. **Credential Cache Duration**
   - 1 week is long (604800 seconds)
   - If system compromised, credentials available for 7 days
   - Mitigation: Cache is memory-only, expires on reboot

4. **URL Conversion**
   - Assumes SSH to HTTPS conversion is safe
   - Potential: Man-in-the-middle attack if HTTPS validation fails
   - Mitigation: Git validates HTTPS certificates by default

### Recommendations

1. **Shorter cache timeout for sensitive repos:**
   ```bash
   git config --local credential.helper 'cache --timeout=3600'  # 1 hour
   ```

2. **Use SSH keys with passphrase + ssh-agent:**
   ```bash
   ssh-add ~/.ssh/id_rsa  # Unlock key
   ```

3. **Prefer HTTPS with token over password:**
   ```bash
   git remote set-url origin https://user:token@github.com/repo.git
   ```

4. **Review auto-update logs:**
   ```bash
   cat ~/.local/share/aios/config.json
   ```

---

## 5. DEBUGGING AUTHENTICATION ISSUES

### SSH Key Issues

```bash
# Test SSH connectivity
ssh -o BatchMode=yes git@github.com

# Check SSH key availability
ssh-add -l

# Add SSH key if needed
ssh-add ~/.ssh/id_rsa
```

### HTTPS Credential Issues

```bash
# Check credential helper
git config credential.helper

# Test credential cache
git credential fill
# Type: host=github.com
# Type: protocol=https
# Press Ctrl+D twice

# Clear credential cache
git credential reject host=github.com protocol=https
```

### Auto-Update Debugging

```bash
# Check config
cat ~/.local/share/aios/config.json

# Check remote URL
git -C ~/projects/aios remote get-url origin

# Manual update with verbose output
python3 ~/projects/aios/aios.py --update

# Check last update time (Unix timestamp)
date -d @1761280352  # Converts timestamp to readable date
```

---

## 6. GIT OPERATIONS FLOW DIAGRAM

```
User Command
    ↓
auto_update() [aio.py: 10-46]
    ├─ Check if in git repo
    ├─ Get remote URL
    └─ Try fetch with SSH env vars
        ├─ If SSH fails
        │   ├─ Detect SSH URL format
        │   ├─ Convert to HTTPS
        │   ├─ Set remote URL
        │   ├─ Configure credential cache
        │   └─ Retry fetch (HTTPS)
        │
        └─ If success
            ├─ Check for updates (HEAD..origin/main)
            ├─ Pull and reinstall if needed
            └─ Continue with main program

Worktree Push [aio.py: 2042-2180]
    ├─ Commit in worktree
    ├─ Switch to main branch
    ├─ Merge worktree branch
    └─ Push to origin/main
        ├─ Uses git's default auth
        └─ May prompt if no credentials

Manual Pull [aio.py: 2180-2201]
    ├─ Show destructive warning
    ├─ Get user confirmation
    ├─ Fetch origin
    ├─ Hard reset to origin/main
    └─ Clean working directory
        ├─ Uses git's default auth
        └─ May use HTTPS cache
```

---

## 7. CONCLUSION

AIOS implements a **two-tiered authentication system:**

1. **Auto-Update (aios.py):** Robust, non-interactive, with fallback
   - Non-interactive environment variables
   - SSH to HTTPS automatic conversion
   - Credential helper caching
   - Timeout protection

2. **Manual Operations (aio.py):** Simple, relies on pre-configuration
   - Direct git operations
   - Uses git's default authentication
   - May prompt user for credentials
   - Simpler code, more straightforward

The key innovation is **automatic SSH-to-HTTPS fallback**, allowing background updates to work without SSH key setup while maintaining security through credential caching.

