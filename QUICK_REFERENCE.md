# AIOS Quick Reference Guide

## What is AIOS?
Portfolio-level AI agent orchestrator that runs multiple AI agents (Codex, Claude, Gemini) across saved projects with isolated git worktrees.

---

## Quick Start

### Single Agent
```bash
aio c                    # Start Codex session
aio l                    # Start Claude session
aio g                    # Start Gemini session
aio cp                   # Codex with editable prompt
aio cpp                  # Codex with auto-run prompt
```

### Multiple Agents (Single Project)
```bash
aio multi c:3 "task"     # 3 Codex in parallel
aio multi c:2 l:1        # 2 Codex + 1 Claude (default protocol)
aio multi c:3 --seq "x"  # 3 Codex sequential
```

### All Projects (Portfolio Operation)
```bash
aio all c:2              # 2 Codex per project (parallel, default protocol)
aio all c:2 "task"       # 2 Codex with custom task
aio all c:2 --seq        # Sequential across projects
aio all c:1 l:1 g:1      # Mixed agents per project
```

---

## Git Operations

### Push (Commit + Push)
```bash
aio push                 # Default message
aio push "message"       # Custom message
aio push -y              # Skip confirmation
```

### Pull (Destructive - Replace Local)
```bash
aio pull                 # Requires confirmation
aio pull -y              # Force without confirmation
```

### Setup Repository
```bash
aio setup                # Initialize new repo
aio setup <url>          # Add remote URL
```

### Revert Commits
```bash
aio revert               # Undo last commit
aio revert 3             # Undo last 3 commits
```

---

## Worktree Management

### List & Navigate
```bash
aio w                    # List all worktrees
aio w0                   # Open worktree 0
aio w<pattern>           # Open by name pattern
```

### Remove Worktree
```bash
aio w+ 0                 # Remove (no push)
aio w+ 0 -y              # Remove (skip confirmation)
aio w++ 0                # Remove and push to main
aio w++ 0 "msg"          # Remove and push with message
```

---

## Project Management

### Projects
```bash
aio p                    # List saved projects
aio 0                    # Open project 0
aio add /path            # Add new project
aio remove 0             # Remove project
```

### Sessions
```bash
aio c 0                  # Codex in project 0
aio l 0 -w               # Claude in project 0, new window
aio g 2 -t               # Gemini in project 2 + terminal
```

---

## Monitoring

### Jobs & Status
```bash
aio jobs                 # All active work
aio jobs --running       # Only running (filter review)
aio jobs -r              # Same (short form)
```

### Session Management
```bash
aio ls                   # List all tmux sessions
aio cleanup              # Delete all worktrees (confirm)
aio cleanup --yes        # Delete all (no confirm)
aio x                    # Kill all tmux sessions
```

---

## Agent Specifications

### Agent Keys
- `c` = Codex (GPT-5 Codex with high reasoning)
- `l` = Claude (Anthropic Claude)
- `g` = Gemini (Google Gemini with --yolo)
- `h` = htop (system monitor)
- `t` = top (basic system monitor)

### Syntax Variants
- `c` = Base command
- `cp` = Base + editable prompt
- `cpp` = Base + auto-run prompt
- `c++` = Base + new worktree

---

## Database & Configuration

### Data Locations
- **Database:** `~/.local/share/aios/aio.db`
- **Config:** `~/.local/share/aios/config.json` (auto-update settings)
- **Worktrees:** `~/projects/aiosWorktrees/` (configurable)

### Auto-Update
```bash
aio --auto-update-on     # Enable background updates
aio --auto-update-off    # Disable
```

---

## Key Differences

### 'multi' vs 'all'
| Aspect | multi | all |
|--------|-------|-----|
| **Scope** | Single project | All saved projects |
| **Scale** | N × 1 | N × M (M = projects) |
| **Use Case** | Quick task | Portfolio-wide optimization |
| **Naming** | `-multi-` suffix | `-all-` suffix |

### 'push' vs 'pull'
| Operation | push | pull |
|-----------|------|------|
| **Action** | Commit & push | Fetch & reset |
| **Direction** | Local → Remote | Remote → Local |
| **Destructive** | No | **YES** (deletes local) |
| **Requires Confirmation** | No | Yes (unless -y) |

---

## Default Prompt (11-Step Protocol)

When no prompt provided, uses 11-step optimization protocol:
1. Read project and ultrathink
2. Write changes (library glue style, minimal lines)
3. Run manually and verify
4-11. [Additional optimization steps]

---

## Worktree Naming

Format: `{agent}-{YYYYMMDD}-{HHMMSS}-{source}-{instance}`

Examples:
- `codex-20251101-134530-all-0` (all command)
- `claude-20251101-134530-multi-1` (multi command)
- `gemini-20251101-134530-multiN` (multi with N projects)

---

## Environment & Git Auth

### Non-Interactive Git
All operations disable prompts:
- `GIT_TERMINAL_PROMPT=0`
- `GIT_ASKPASS=echo`
- `SSH_ASKPASS=echo`
- `GIT_SSH_COMMAND='ssh -oBatchMode=yes'`
- `stdin=subprocess.DEVNULL`

### Credential Handling
- **Method:** Git credential helper cache
- **Duration:** 1 week (604800 seconds)
- **Fallback:** SSH → HTTPS automatic conversion
- **Storage:** Memory only (not disk)

---

## File Locations (Key Paths)

| Item | Path |
|------|------|
| Main Script | `/home/seanpatten/projects/aios/aio.py` |
| Database | `~/.local/share/aios/aio.db` |
| Config | `~/.local/share/aios/config.json` |
| Worktrees | `~/projects/aiosWorktrees/` |
| Projects | Stored in database |

---

## Useful Commands Summary

```bash
# Monitor
aio jobs                 # View all work
aio w                    # List worktrees

# Run agents
aio all c:2 "optimize"   # Portfolio operation
aio multi 0 c:2 "task"   # Single project operation

# Git
aio push "message"       # Commit and push
aio pull -y              # Force pull

# Manage
aio add /path            # Add project
aio p                    # List projects
aio cleanup --yes        # Delete all worktrees
```

---

## Setup Instructions

### Installation
```bash
cd /path/to/aios
aio install              # Install as global command
```

### First Run
```bash
aio add /path/to/project1
aio add /path/to/project2
aio p                    # Verify projects
aio all c:1 "test"       # Try portfolio operation
```

---

## Architecture at a Glance

```
User Input
    ↓
Auto-update (fast-forward)
    ↓
Database (projects, sessions, config)
    ↓
Command Router (help, all, multi, push, pull, etc.)
    ↓
Worktree Creation
    ↓
Tmux Session Launcher
    ↓
Background Update Thread (if enabled)
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `aio` command not found | Run `aio install` to create symlink |
| Worktrees not found | Check `~/projects/aiosWorktrees/` exists |
| Push fails | Ensure git remote configured or run `aio setup` |
| Pull asks for auth | Normal - first HTTPS operation caches credentials |
| Auto-update failing | Check `~/.local/share/aios/config.json` |

---

## Key Code Locations

| Feature | File | Lines |
|---------|------|-------|
| Auto-update | aio.py | 10-46 |
| Database init | aio.py | 68-154 |
| Multi command | aio.py | 1599-1728 |
| All command | aio.py | 1729-1900 |
| Push operation | aio.py | 2042-2180 |
| Pull operation | aio.py | 2180-2201 |
| Worktree create | aio.py | 1206-1270 |

