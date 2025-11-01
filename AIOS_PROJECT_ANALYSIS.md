# AIOS Project Comprehensive Analysis

## 1. PROJECT OVERVIEW

### Purpose
AIOS is an **AI agent session manager** designed to orchestrate multiple AI agent instances (Codex, Claude, Gemini) across git repositories with isolated worktrees. It serves as a workflow automation tool with portfolio-level operations across multiple projects.

### Key Characteristics
- **Multi-agent orchestration**: Run multiple AI agents in parallel or sequentially
- **Git-integrated**: Creates isolated worktrees for each agent instance
- **Portfolio-level operations**: Coordinate agents across all saved projects
- **Event-driven architecture**: Uses tmux sessions and pexpect for automation
- **Database-backed**: SQLite with WAL mode for persistent configuration
- **Auto-updating**: Git-based self-update mechanism with HTTPS fallback

### Core Technologies
- **Python 3** with subprocess and pexpect
- **tmux**: Terminal multiplexer for session management
- **SQLite3**: Database for configuration and state persistence
- **git**: Version control with automatic updates

---

## 2. MAIN ENTRY POINT AND ARCHITECTURE

### File Structure
```
/home/seanpatten/projects/aios/
├── aio.py                  # Main script (2,432 lines) - CURRENT VERSION
├── aios.py                 # Legacy version (1,410 lines)
├── README.md               # Comprehensive documentation
├── CHANGELOG.md            # Version history
├── data/
│   ├── config.json        # Runtime configuration (auto-update settings)
│   ├── aio.db             # SQLite database (projects, sessions, config)
│   └── timings.json       # Performance baselines
├── jobs/                  # Job execution directories
│   └── {agent}-{timestamp}/
│       └── worktree/      # Git worktree (isolated branch)
└── tasks/                 # Task definition files (.json)
```

### Database Schema (aio.db)
```sql
-- Configuration storage
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
)

-- Saved projects
CREATE TABLE projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL,
    display_order INTEGER NOT NULL UNIQUE
)

-- Session templates
CREATE TABLE sessions (
    key TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    command_template TEXT NOT NULL
)
```

### Configuration (data/config.json)
```json
{
  "auto_update": true,
  "last_update_check": 1761280352,
  "check_interval": 604800,  // 1 week in seconds
  "editor_command": "code ."
}
```

---

## 3. KEY FEATURES AND COMMANDS

### 3.1 Session Management
Sessions represent different AI agents with configurable prompts.

**Available Agent Keys:**
- `c` = Codex (GPT-5 Codex with reasoning)
- `l` = Claude (Anthropic Claude)
- `g` = Gemini (Google Gemini)
- `h` = htop (system monitor)
- `t` = top (basic system monitor)

**Session Commands:**
```bash
aio c                # Attach to/create Codex session
aio cp               # Codex with editable prompt
aio cpp              # Codex with auto-executed prompt
aio c 0              # Codex in project 0
aio c -w             # Codex in new window
aio l++              # Claude in new worktree
aio g++ 2            # Gemini in project 2 worktree
```

### 3.2 Push Command
Commits and pushes changes to the main branch.

**Location:** Lines 2042-2180 in aio.py

**Syntax:**
```bash
aio push                    # Push with default message
aio push "Custom message"   # Push with custom message
aio push -y                 # Skip confirmation
```

**Behavior:**
1. Detects if in a worktree or regular repository
2. If in worktree:
   - Creates a commit with message
   - Switches to main branch
   - Merges worktree branch into main
   - Pushes to origin/main
3. If in regular repo:
   - Stages all changes
   - Creates a commit
   - Pushes to default upstream

**Git Operations:**
```python
sp.run(['git', '-C', cwd, 'add', '-A'])
sp.run(['git', '-C', cwd, 'commit', '-m', commit_msg])
sp.run(['git', '-C', cwd, 'push'], capture_output=True)
```

### 3.3 Pull Command
Destructive operation - replaces local with server version.

**Location:** Lines 2180-2201 in aio.py

**Syntax:**
```bash
aio pull           # Pull with confirmation warning
aio pull -y        # Force without confirmation
```

**Behavior:**
1. Displays warning: "This will DELETE all local changes!"
2. Requires user confirmation (unless -y flag)
3. Fetches from origin
4. Performs hard reset to origin/main (or origin/master)
5. Cleans untracked files

**Git Operations:**
```python
sp.run(['git', '-C', cwd, 'fetch', 'origin'])
sp.run(['git', '-C', cwd, 'reset', '--hard', 'origin/main'])
sp.run(['git', '-C', cwd, 'clean', '-f', '-d'])
```

### 3.4 Git Operations Command
Includes setup, push, pull, and revert functionality.

**Setup Command:**
```bash
aio setup                    # Initialize repo
aio setup https://github... # Add remote and initialize
```

Creates initial commit, sets up origin remote, and pushes to main branch.

**Revert Command:**
```bash
aio revert          # Undo last commit
aio revert 3        # Undo last 3 commits
```

---

## 4. GIT AUTHENTICATION HANDLING

### 4.1 Auto-Update Authentication (aios.py)

**Location:** Lines 892-1011 in aios.py (legacy version)

**Problem:** Auto-updates from git repositories require authentication, but cannot prompt interactively.

**Solution: Non-Interactive Git Environment**

The code sets environment variables to disable all interactive prompts:

```python
git_env = os.environ.copy()
git_env['GIT_TERMINAL_PROMPT'] = '0'      # Disable terminal prompts
git_env['GIT_ASKPASS'] = 'echo'           # Disable password prompts
git_env['SSH_ASKPASS'] = 'echo'           # Disable SSH password prompts
git_env['GIT_SSH_COMMAND'] = 'ssh -oBatchMode=yes'  # Batch mode (no interactive auth)
```

**Stdin Prevention:**
All git operations include `stdin=subprocess.DEVNULL` to prevent hanging on auth prompts.

### 4.2 HTTPS Fallback Mechanism

**Location:** Lines 931-954 in aios.py

**Problem:** SSH authentication may fail without user interaction.

**Solution: Automatic SSH to HTTPS Conversion**

```python
# If SSH fetch fails:
if result.returncode != 0 and is_ssh and auto:
    # Convert SSH URL to HTTPS
    https_url = remote_url
    if '@' in https_url and ':' in https_url:
        parts = https_url.split('@')[1].split(':')
        if len(parts) == 2:
            https_url = f"https://{parts[0]}/{parts[1]}"
    
    # Permanently switch remote
    sp.run(['git', '-C', str(install_dir), 'remote', 'set-url', 'origin', https_url])
    
    # Configure credential caching for 1 week
    sp.run(['git', '-C', str(install_dir), 'config', 
           'credential.helper', 'cache --timeout=604800'])
```

**URL Conversion Examples:**
- `git@github.com:user/repo.git` → `https://github.com/user/repo.git`
- `ssh://git@gitlab.com/user/repo.git` → `https://gitlab.com/user/repo.git`

### 4.3 Credential Management

**Method:** Git credential helper cache

**Configuration:**
```bash
git config credential.helper 'cache --timeout=604800'
```

**Characteristics:**
- Memory-only storage (not disk)
- 1 week expiration (604800 seconds)
- Per-repository scope
- Requires local system access
- Automatic prompt on first HTTPS operation

**Key Security Features:**
- No SSH keys required
- No disk storage of credentials
- Silent failure on network errors
- Non-blocking operation

### 4.4 Authentication Flow in aio.py

**Current Version (aio.py):**
The newer aio.py version (2,432 lines) implements simpler git authentication:
- Direct subprocess calls with `capture_output=True`
- Relies on git's configured credentials
- No explicit HTTPS fallback in main script
- Works with pre-configured git remotes

**Git Operations Pattern:**
```python
result = sp.run(['git', '-C', project_path, 'push', 'origin', 'main'],
               capture_output=True, text=True)
if result.returncode == 0:
    print("✓ Pushed to remote")
else:
    error_msg = result.stderr.strip()
    print(f"✗ Push failed: {error_msg}")
```

---

## 5. THE "ALL" COMMAND: PORTFOLIO-LEVEL OPERATIONS

### 5.1 Purpose and Scope

**What it does:** Runs specified agents across ALL saved projects in parallel or sequentially.

**Location:** Lines 1729-1900 in aio.py

**Key Difference from "multi" command:**
- `multi`: Runs agents in a SINGLE project
- `all`: Runs agents in ALL saved projects

### 5.2 Command Syntax

```bash
# Basic usage
aio all c:2                      # 2 Codex per project (parallel, default prompt)
aio all c:2 "custom task"       # 2 Codex with custom task
aio all c:1 l:1                 # 1 Codex + 1 Claude per project
aio all c:2 --seq "task"        # Sequential across projects
aio all c:2 g:1 l:1 "optimize"  # Mixed agents per project
```

**Agent Specs Format:**
- Format: `<agent_key>:<count>`
- Examples: `c:2`, `l:1`, `g:3`
- Repeatable: `aio all c:2 l:1 g:1`

**Flags:**
- `--seq` or `--sequential`: Run one project at a time
- Default: Parallel execution across all projects

### 5.3 Default Prompt (11-Step Protocol)

When no prompt provided, uses CODEX_PROMPT:

```
Step 1: Read project and relevant files. Then ultrathink how to best solve this.
Step 2: Write changes (library glue style, minimal lines)
Step 3: Run manually and verify output
...
(11 total steps - optimization protocol)
```

### 5.4 Implementation Details

**Execution Model:**

```python
for project_idx, project_path in enumerate(PROJECTS):
    project_name = os.path.basename(project_path)
    
    for agent_key, count in agent_specs:
        base_name, base_cmd = sessions.get(agent_key)
        
        for instance_num in range(count):
            # Create unique worktree name
            worktree_name = f"{base_name}-{date_str}-{time_str}-all-{instance_num}"
            
            # Create worktree
            worktree_path = create_worktree(project_path, worktree_name)
            
            # Construct command with prompt baked in
            full_cmd = f'{base_cmd} "{escaped_prompt}"'
            
            # Create tmux session
            sp.run(['tmux', 'new', '-d', '-s', session_name, '-c', worktree_path, full_cmd])
```

**Key Features:**
1. Unique naming: `{agent}-{date}-{time}-all-{instance}`
2. Isolated worktrees for each agent
3. Prompt baked into command (not sent via stdin)
4. Non-blocking: returns immediately, agents run in background

### 5.5 Difference from "multi" Command

**Comparison Table:**

| Aspect | multi | all |
|--------|-------|-----|
| **Scope** | Single project | All saved projects |
| **Project Selection** | Optional project # or current dir | Fixed: all projects |
| **Syntax** | `aio multi [#] c:N "prompt"` | `aio all c:N "prompt"` |
| **Worktree Naming** | `{agent}-{timestamp}-multi-{#}` | `{agent}-{timestamp}-all-{#}` |
| **Use Case** | Quick multi-agent on one project | Portfolio-wide analysis/optimization |
| **Scale** | N instances × 1 project | N instances × M projects |

**Example:**
```bash
# multi: 3 Codex on one project
aio multi 0 c:3 "optimize code"
# Launches: 3 sessions, all in project 0

# all: 3 Codex on all projects
aio all c:3 "optimize code"
# Launches: 3 × M sessions (M = number of projects)
```

### 5.6 Sequential Mode (`--seq`)

When `--seq` flag provided:
- Agents for project 0 all complete before project 1 starts
- Useful for resource-constrained environments
- Default (no flag): All agents run in parallel

```bash
aio all c:2 --seq "task"
# Output:
# Project 0: [Running 2 Codex] → wait for completion
# Project 1: [Running 2 Codex] → wait for completion
# Project 2: [Running 2 Codex] → wait for completion
```

### 5.7 Monitoring and Control

**View all running agents:**
```bash
aio jobs              # Show all active work
aio jobs --running    # Filter out 'review' status
```

**Attach to specific agent:**
```bash
tmux attach -t {session_name}  # Attach to tmux session
aio w                          # List all worktrees
aio w0                         # Open worktree 0
```

---

## 6. WORKTREE MANAGEMENT

### 6.1 Worktree Structure

**Path:** `~/projects/aiosWorktrees/`

**Naming Convention:**
- `{agent}-{YYYYMMDD}-{HHMMSS}-{source}-{instance}`
- Source: `multi`, `all`, or project-specific
- Example: `codex-20251101-134530-all-0`

### 6.2 Worktree Commands

```bash
aio w                    # List all worktrees
aio w0                   # Open worktree 0
aio w+ 0                 # Remove worktree (no push)
aio w++ 0                # Remove and push to main
aio w++ 0 "message"      # Remove and push with custom message
aio w++ 0 --yes          # Skip confirmation
```

**Push Operation (w++):**
1. Switches to main branch
2. Merges worktree branch
3. Pushes to origin/main
4. Deletes worktree and branch

---

## 7. PROJECT CONFIGURATION AND MANAGEMENT

### 7.1 Saved Projects

Projects are stored in database and can be:
```bash
aio add /path/to/project   # Add project
aio p                      # List all projects
aio remove 0               # Remove project by index
aio 0                      # Open project 0 in shell
```

### 7.2 Session Templates

Default sessions defined in database:

| Key | Name | Command |
|-----|------|---------|
| `c` | codex | `codex -c model_reasoning_effort="high" --model gpt-5-codex --dangerously-bypass-approvals-and-sandbox` |
| `cp` | codex-p | Above + `"{CODEX_PROMPT}"` |
| `l` | claude | `claude --dangerously-skip-permissions` |
| `lp` | claude-p | Above + `"{CLAUDE_PROMPT}"` |
| `g` | gemini | `gemini --yolo` |
| `gp` | gemini-p | Above + `"{GEMINI_PROMPT}"` |
| `h` | htop | `htop` |
| `t` | top | `top` |

### 7.3 Prompt Customization

Each session type has a configurable prompt:
- Stored in database `config` table
- Keys: `claude_prompt`, `codex_prompt`, `gemini_prompt`
- Default: 3-step optimization protocol (library glue style)

---

## 8. AUTO-UPDATE MECHANISM

### 8.1 Background Update Check

**Location:** aios.py lines 1013-1027

**Flow:**
```
1. User runs `aio` command
2. auto_update_check() called
3. If auto_update enabled in config:
   - Check if check_interval seconds passed
   - If yes: spawn background thread
   - Main program continues immediately
4. Background thread:
   - Calls self_update(silent=True, auto=True)
   - Updates silently
   - Saves new check timestamp
```

### 8.2 Update Process

**Step 1: Check Remote URL**
```python
result = sp.run(['git', '-C', SCRIPT_DIR, 'remote', 'get-url', 'origin'])
is_ssh = remote_url.startswith('git@') or remote_url.startswith('ssh://')
```

**Step 2: Try Fetch (SSH)**
```python
result = sp.run(['git', '-C', SCRIPT_DIR, 'fetch', 'origin'],
              env=git_env, timeout=10)
```

**Step 3: HTTPS Fallback if SSH Fails**
```python
if result.returncode != 0 and is_ssh and auto:
    # Convert and retry with HTTPS
    sp.run(['git', 'remote', 'set-url', 'origin', https_url])
    result = sp.run(['git', '-C', SCRIPT_DIR, 'fetch', 'origin'])
    if result.returncode == 0:
        # Configure credential cache
        sp.run(['git', 'config', 'credential.helper', 'cache --timeout=604800'])
```

**Step 4: Check if Updates Available**
```python
result = sp.run(['git', '-C', SCRIPT_DIR, 'rev-list', 'HEAD..origin/main', '--count'])
commits_behind = int(result.stdout.strip())
```

**Step 5: Pull and Reinstall**
```python
sp.run(['git', '-C', SCRIPT_DIR, 'pull', 'origin', 'main'])
sp.run(['pip', 'install', '-e', SCRIPT_DIR])  # If installed via pip
```

### 8.3 Configuration

Enable/disable updates:
```bash
aios --auto-update-on    # Enable (check once per day/week)
aios --auto-update-off   # Disable
```

Check interval (in seconds):
- Default: 604800 (1 week)
- Configurable in `data/config.json`

---

## 9. ARCHITECTURE SUMMARY

### Data Flow Diagram
```
User Command
    ↓
auto_update() [fast-forward only]
    ↓
Database Load (config, projects, sessions)
    ↓
Command Parser (help, install, watch, send, multi, all, jobs, push, pull, etc.)
    ↓
Session/Worktree Management
    ↓
Git Operations (if needed)
    ↓
Tmux Session Creation
    ↓
Background Background Auto-Update Thread (if enabled)
```

### Key Components

| Component | Purpose | Location |
|-----------|---------|----------|
| auto_update() | Auto-pull latest from git | aio.py:10-46 |
| WALManager | SQLite connection with WAL | aio.py:52-66 |
| init_database() | Schema creation and defaults | aio.py:68-154 |
| load_config() | Read database config | aio.py:156-161 |
| load_projects() | Get saved project paths | aio.py:163-168 |
| load_sessions() | Build session command map | aio.py:224-295 |
| create_worktree() | Create isolated git branch | aio.py:1206-1270 |
| find_worktree() | Search worktrees by pattern | aio.py:1014-1038 |
| remove_worktree() | Delete worktree and branch | aio.py:1052-1205 |
| send_prompt_to_session() | Send command to tmux | aio.py:647-719 |
| 'multi' handler | Single-project agents | aio.py:1599-1728 |
| 'all' handler | Portfolio-level agents | aio.py:1729-1900 |
| 'push' handler | Commit and push | aio.py:2042-2180 |
| 'pull' handler | Destructive reset | aio.py:2180-2201 |
| 'setup' handler | Initialize repo | aio.py:2233-2338 |

---

## 10. KEY DESIGN PRINCIPLES

1. **Git-Inspired:** Single-file design (like git), self-updating, minimal commands
2. **Event-Driven:** Uses tmux sessions and pexpect, no polling
3. **Non-Interactive:** All git operations have timeouts, no prompts
4. **Portfolio-Level:** Coordinate across multiple projects simultaneously
5. **Automatic Updates:** Silent background updates with credential caching
6. **Worktree Isolation:** Each agent gets isolated git branch
7. **Database-Backed:** SQLite for persistent configuration
8. **Resilient:** HTTPS fallback, credential caching, error handling

---

## 11. CONCLUSION

AIOS is a sophisticated **portfolio-level AI agent orchestrator** built on:
- **Git integration** with automatic self-updates and HTTPS fallback
- **Worktree isolation** for safe, parallel agent execution
- **Multi-agent coordination** via the `all` and `multi` commands
- **Non-interactive authentication** through credential caching
- **Database persistence** for projects, sessions, and configuration

The `push`, `pull`, and git operations provide standard version control integration, while the `all` command enables running N agents across M projects simultaneously with the 11-step optimization protocol as the default task.

