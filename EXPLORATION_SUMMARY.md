# AIOS Project Exploration - Complete Summary

## Documents Generated

This exploration produced three comprehensive analysis documents:

1. **AIOS_PROJECT_ANALYSIS.md** - Complete technical analysis (11 sections)
2. **QUICK_REFERENCE.md** - Command reference and quick start guide
3. **GIT_AUTH_DEEP_DIVE.md** - Detailed git authentication mechanisms
4. **EXPLORATION_SUMMARY.md** - This document

---

## Key Findings

### 1. Project Purpose

**AIOS** is a **Portfolio-Level AI Agent Orchestrator** that:
- Runs multiple AI agents (Codex, Claude, Gemini) across saved projects
- Creates isolated git worktrees for each agent
- Provides both single-project (`multi`) and portfolio-wide (`all`) operations
- Implements automatic self-updates with SSH-to-HTTPS fallback
- Uses SQLite for persistent configuration

### 2. Main Commands

#### Session Management
```bash
aio c              # Start Codex session
aio l              # Start Claude session
aio g              # Start Gemini session
aio cp/cpp         # Codex with prompt variants
```

#### Multi-Agent Operations
```bash
aio multi c:3 "task"         # 3 agents in single project
aio all c:2 l:1 "optimize"   # 2 Codex + 1 Claude in ALL projects
```

#### Git Operations
```bash
aio push "message"   # Commit and push
aio pull -y          # Destructive reset (replace local with remote)
aio setup <url>      # Initialize repo
aio revert 3         # Undo last 3 commits
```

#### Worktree Management
```bash
aio w                # List worktrees
aio w0               # Open worktree 0
aio w++ 0 "msg"      # Remove and push with message
```

### 3. Key Differences: "all" vs "multi"

| Aspect | multi | all |
|--------|-------|-----|
| **Scope** | Single project | All saved projects |
| **Syntax** | `aio multi [#] c:N "task"` | `aio all c:N "task"` |
| **Scale** | N instances × 1 project | N instances × M projects |
| **Use Case** | Quick multi-agent task | Portfolio-wide optimization |
| **Worktree Naming** | `-multi-` suffix | `-all-` suffix |
| **Default Task** | Optional custom prompt | 11-step optimization protocol |

**Example:**
```bash
# Launch 3 Codex in project 0 only
aio multi 0 c:3 "optimize code"

# Launch 3 Codex in EVERY saved project
aio all c:3 "optimize code"
```

### 4. Git Authentication Mechanisms

#### Auto-Update Authentication (aios.py - Legacy)

**Problem:** Background updates cannot prompt user for credentials

**Solution: Four-Layer Approach**

1. **Non-Interactive Environment Variables**
   ```python
   GIT_TERMINAL_PROMPT=0          # Disable prompts
   GIT_ASKPASS=echo               # Dummy credential provider
   SSH_ASKPASS=echo               # Dummy SSH password provider
   GIT_SSH_COMMAND='ssh -oBatchMode=yes'  # Batch mode
   stdin=subprocess.DEVNULL       # Prevent stdin reads
   ```

2. **Timeout Protection**
   - All git operations have 5-30 second timeouts
   - Prevents hanging if auth needed

3. **SSH to HTTPS Automatic Fallback**
   - Detects SSH remote URL
   - Converts to HTTPS if SSH fails
   - Examples:
     - `git@github.com:user/repo.git` → `https://github.com/user/repo.git`
     - `ssh://git@gitlab.com/path` → `https://gitlab.com/path`
   - Permanently switches remote (`git remote set-url`)

4. **Credential Caching**
   ```bash
   git config credential.helper 'cache --timeout=604800'
   ```
   - Memory-only storage (not disk)
   - 1 week expiration (604800 seconds)
   - Automatic prompt on first HTTPS operation
   - Subsequent operations use cache (non-blocking)

#### Manual Git Operations (aio.py - Current)

**Simpler approach:** Relies on pre-configured git credentials
- Direct subprocess calls with `capture_output=True`
- No special environment variables
- No HTTPS fallback logic
- Will prompt if credentials needed (user is present)
- Assumes SSH keys or HTTPS cache already set up

**Difference:** aio.py assumes user has already configured git authentication, while aios.py implements robust fallback for unattended background operations.

### 5. Project Architecture

#### File Organization
```
/home/seanpatten/projects/aios/
├── aio.py                          # Main script (2,432 lines)
├── aios.py                         # Legacy (1,410 lines)
├── data/
│   ├── config.json                 # Auto-update config
│   └── aio.db                      # SQLite database
├── jobs/                           # Job execution directories
└── tasks/                          # Task JSON files
```

#### Database Schema
```sql
config    -- Key-value config storage
projects  -- Saved project paths
sessions  -- Session templates (codex, claude, gemini, etc)
```

#### Default Configuration
```json
{
  "auto_update": true,
  "last_update_check": 1761280352,
  "check_interval": 604800,  // 1 week
  "editor_command": "code ."
}
```

### 6. Execution Flow

```
User Command (aio all c:2 "task")
    ↓
auto_update()                        [aio.py:10-46]
    ├─ Check if in git repo
    ├─ Pull latest with --ff-only
    └─ Re-exec if updated
    ↓
Database Load                        [aio.py:68-295]
    ├─ Load config, projects, sessions
    └─ Initialize SQLite with WAL
    ↓
Command Parser                       [aio.py:1729-1900]
    ├─ Parse 'all' command
    ├─ Validate agent specs
    └─ Use default prompt if needed
    ↓
For Each Project                     [aio.py:1806-1871]
    ├─ Create worktree
    │   └─ git worktree add ...
    ├─ Launch tmux session
    │   └─ tmux new -d -s name cmd
    └─ Track session info
    ↓
Return Control (Non-Blocking)
    └─ Agents run in background
```

### 7. Default Prompts

Each agent type has configurable default prompt:

**Default Protocol (when no prompt provided):**
```
Step 1: Read project and ultrathink solution
Step 2: Write changes (library glue style, minimal lines)
Step 3: Run manually and verify output
Step 4-11: Additional optimization steps
```

**Library Glue Principle:**
- Minimal line count
- Direct library calls
- No custom business logic
- Rely on libraries for functionality
- Event-driven only (no polling)

### 8. Worktree Management

**Naming Convention:**
`{agent}-{YYYYMMDD}-{HHMMSS}-{source}-{instance}`

**Examples:**
- `codex-20251101-134530-all-0`     (all command)
- `claude-20251101-134530-multi-1`   (multi command)

**Lifecycle:**
1. Create: `git worktree add`
2. Use: Agent runs in isolated branch
3. Remove: `aio w+` (no push) or `aio w++` (push + merge)
   - Merges back to main
   - Pushes to origin
   - Deletes worktree and branch

### 9. Session Templates

Default agents:

| Key | Name | Command |
|-----|------|---------|
| `c` | codex | `codex --model gpt-5-codex -c model_reasoning_effort="high"` |
| `cp` | codex-p | Above + `"{CODEX_PROMPT}"` |
| `l` | claude | `claude --dangerously-skip-permissions` |
| `lp` | claude-p | Above + `"{CLAUDE_PROMPT}"` |
| `g` | gemini | `gemini --yolo` |
| `gp` | gemini-p | Above + `"{GEMINI_PROMPT}"` |

**Prompt Variants:**
- `c` = No prompt (manual input)
- `cp` = Editable prompt (can modify before running)
- `cpp` = Auto-run prompt (executes immediately)
- `c++` = New worktree (isolated branch)

### 10. Auto-Update System

**Flow:**
1. User runs `aio` command
2. `auto_update()` checks if in git repo
3. Pulls with `--ff-only` (fast-forward only)
4. Re-execs script if updated
5. Main program continues

**Background Update Check:**
1. Checks if `check_interval` seconds passed
2. If yes, spawns background thread
3. Thread calls `self_update(silent=True, auto=True)`
4. Main program continues immediately

**Key Features:**
- Fast-forward only (safe, no merge conflicts)
- Non-blocking (background thread)
- Silent failure (never blocks startup)
- HTTPS fallback if SSH fails
- Credential caching (1 week)

---

## Important Code Locations

### Core Functions
- **Auto-update:** aio.py lines 10-46
- **Database init:** aio.py lines 68-154
- **Load config:** aio.py lines 156-161
- **Load projects:** aio.py lines 163-168
- **Load sessions:** aio.py lines 224-295
- **Create worktree:** aio.py lines 1206-1270
- **Find worktree:** aio.py lines 1014-1038
- **Remove worktree:** aio.py lines 1052-1205

### Command Handlers
- **'all' command:** aio.py lines 1729-1900
- **'multi' command:** aio.py lines 1599-1728
- **'push' command:** aio.py lines 2042-2180
- **'pull' command:** aio.py lines 2180-2201
- **'setup' command:** aio.py lines 2233-2338
- **'revert' command:** aio.py lines 2202-2232

### Git Authentication (aios.py - Legacy)
- **Auto-update mechanism:** aios.py lines 892-1011
- **Non-interactive setup:** aios.py lines 905-908
- **HTTPS fallback:** aios.py lines 931-954
- **Credential caching:** aios.py lines 952-954
- **Background check:** aios.py lines 1013-1027

---

## Design Principles

1. **Git-Inspired**
   - Single-file design (like git itself)
   - Self-updating mechanism
   - Minimal, focused commands

2. **Event-Driven**
   - Uses tmux sessions (not polling)
   - Uses pexpect for automation
   - Background threads for non-blocking operations

3. **Non-Interactive**
   - All git operations have timeouts
   - Environment variables disable prompts
   - stdin=DEVNULL prevents hanging

4. **Portfolio-Level**
   - Can coordinate across multiple projects
   - Parallel or sequential execution
   - Scale from N×1 (multi) to N×M (all)

5. **Resilient**
   - HTTPS fallback for authentication
   - Credential caching for HTTPS
   - Silent failure on network errors
   - Database-backed configuration

---

## Security Considerations

### Strengths
- Credential cache is memory-only (not disk)
- SSH → HTTPS fallback eliminates SSH key requirement
- Timeouts prevent hanging
- Non-interactive prevents prompts

### Concerns
- 1-week credential cache duration (long)
- Potential plain text in error logs
- Requires system security (if compromised, credentials available)

### Recommendations
1. Use shorter cache timeout for sensitive repos: `git config --local credential.helper 'cache --timeout=3600'`
2. Use SSH keys with passphrase + ssh-agent
3. Prefer HTTPS with token over password
4. Review auto-update settings regularly

---

## Quick Reference Commands

```bash
# Single agent
aio c                    # Start Codex

# Multiple agents (single project)
aio multi c:3 "task"     # 3 Codex
aio multi c:2 l:1        # 2 Codex + 1 Claude

# Portfolio operation (all projects)
aio all c:2              # 2 Codex per project
aio all c:1 l:1 g:1      # Mixed agents
aio all c:2 --seq        # Sequential

# Git operations
aio push "message"       # Commit and push
aio pull -y              # Force pull
aio setup <url>          # Initialize repo

# Worktree management
aio w                    # List
aio w0                   # Open worktree 0
aio w++ 0 "msg"          # Remove and push

# Management
aio jobs                 # View all work
aio p                    # List projects
aio add /path            # Add project
aio cleanup --yes        # Delete all worktrees
```

---

## Conclusion

AIOS is a sophisticated, production-ready **portfolio-level AI agent orchestrator** with:

- **Robust git integration** featuring automatic self-updates and SSH-to-HTTPS fallback
- **Worktre isolation** for safe, parallel agent execution across multiple projects
- **Multi-agent coordination** via `all` (portfolio-wide) and `multi` (single-project) commands
- **Non-interactive authentication** with credential caching and timeout protection
- **Database persistence** for projects, sessions, and configuration
- **Event-driven architecture** using tmux and pexpect (no polling)

The `all` command is the unique feature that enables running N agents across M projects simultaneously, while the push/pull/git commands provide standard version control integration. The authentication system is particularly sophisticated, with automatic fallback from SSH to HTTPS and intelligent credential caching for background operations.

