# AIOS Project Documentation Index

## Overview

This directory contains comprehensive documentation of the AIOS project (AI Agent Orchestrator System). The exploration has generated four detailed analysis documents totaling 1,815 lines of documentation.

---

## Documentation Files

### 1. AIOS_PROJECT_ANALYSIS.md (607 lines)
**Comprehensive Technical Analysis**

Complete deep-dive into all aspects of the AIOS project.

**Contents:**
- Project overview and characteristics
- Main entry point and architecture
- Database schema and configuration
- All key features and commands
- Git authentication handling
- The "all" command (portfolio operations)
- Worktree management
- Project configuration
- Auto-update mechanism
- Architecture summary and components
- Design principles

**Best For:** Understanding complete system architecture

**Key Sections:**
- Section 3: Key Features (push, pull, git commands)
- Section 4: Git Authentication Handling
- Section 5: The "all" Command Deep-Dive
- Section 9: Architecture Summary with code locations

---

### 2. QUICK_REFERENCE.md (309 lines)
**Command Reference and Quick Start Guide**

Fast reference for common commands and usage patterns.

**Contents:**
- What is AIOS (one-liner)
- Quick start examples
- Git operations
- Worktree management
- Project management
- Monitoring and automation
- Agent specifications
- Database and configuration
- Key differences (multi vs all, push vs pull)
- Worktree naming convention
- File locations
- Troubleshooting

**Best For:** Daily reference, remembering command syntax

**Key Tables:**
- 'multi' vs 'all' comparison
- 'push' vs 'pull' comparison
- Agent key definitions
- File locations

---

### 3. GIT_AUTH_DEEP_DIVE.md (508 lines)
**Detailed Git Authentication Mechanisms**

Deep technical analysis of how git authentication works in two contexts.

**Contents:**
- Auto-update authentication (aios.py)
  - Non-interactive environment variables
  - Timeout protection
  - SSH to HTTPS fallback mechanism
  - Credential caching for HTTPS
  
- Manual git operations (aio.py)
  - Push operation details
  - Pull operation details
  - Setup operation details
  
- Authentication comparison (auto vs manual)
- Potential security issues and recommendations
- Debugging authentication issues
- Git operations flow diagram

**Best For:** Understanding git security and authentication flows

**Key Innovations:**
- SSH-to-HTTPS automatic fallback
- Non-interactive environment variable approach
- Credential helper caching (memory-only, 1 week)

---

### 4. EXPLORATION_SUMMARY.md (391 lines)
**Executive Summary of Complete Exploration**

High-level overview with key findings and quick reference.

**Contents:**
- What this exploration produced
- Key findings (10 sections)
  1. Project purpose
  2. Main commands
  3. Key differences: 'all' vs 'multi'
  4. Git authentication mechanisms
  5. Project architecture
  6. Execution flow
  7. Default prompts
  8. Worktree management
  9. Session templates
  10. Auto-update system

- Important code locations
- Design principles
- Security considerations
- Quick reference commands
- Conclusion

**Best For:** Getting oriented, understanding big picture

**Key Quick-Reference:**
- 25+ command examples
- 10 code location tables
- 5 design principles

---

## Quick Navigation

### By Task

**I want to...**

- **Understand what AIOS does**
  → Read: EXPLORATION_SUMMARY.md (Section 1)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 1)

- **Use the 'all' command**
  → Read: QUICK_REFERENCE.md (All Projects section)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 5)
  → Read: EXPLORATION_SUMMARY.md (Section 3)

- **Understand push/pull commands**
  → Read: QUICK_REFERENCE.md (Git Operations)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 3.2-3.3)

- **Debug git authentication issues**
  → Read: GIT_AUTH_DEEP_DIVE.md (Section 5)
  → Read: GIT_AUTH_DEEP_DIVE.md (Section 6)

- **Learn git auth architecture**
  → Read: GIT_AUTH_DEEP_DIVE.md (Sections 1-3)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 4)

- **Find code locations**
  → Read: EXPLORATION_SUMMARY.md (Important Code Locations)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 9 - Key Components)

- **Compare multi vs all**
  → Read: QUICK_REFERENCE.md (Key Differences table)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 5.5)
  → Read: EXPLORATION_SUMMARY.md (Section 3)

- **Understand worktrees**
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 6)
  → Read: QUICK_REFERENCE.md (Worktree Management)

- **Set up AIOS**
  → Read: QUICK_REFERENCE.md (Setup Instructions)
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 7)

- **Understand database schema**
  → Read: AIOS_PROJECT_ANALYSIS.md (Section 2)
  → Read: EXPLORATION_SUMMARY.md (Section 5)

---

## Key Findings Summary

### Project Type
Portfolio-level AI agent orchestrator with git integration

### Main Distinguishing Feature
**The "all" command:** Runs N agents across M projects simultaneously

### Key Technologies
- Python 3 with subprocess and pexpect
- tmux for session management
- SQLite3 for persistence
- git with automatic self-updates

### Authentication Innovation
SSH-to-HTTPS automatic fallback for background updates

### Architecture Style
Event-driven (no polling), git-inspired design

---

## Code Locations Quick Reference

| Feature | File | Lines |
|---------|------|-------|
| Auto-update entry | aio.py | 10-46 |
| Database setup | aio.py | 68-154 |
| 'all' command | aio.py | 1729-1900 |
| 'multi' command | aio.py | 1599-1728 |
| Push operation | aio.py | 2042-2180 |
| Pull operation | aio.py | 2180-2201 |
| Worktree create | aio.py | 1206-1270 |
| Git auth (legacy) | aios.py | 892-1011 |
| HTTPS fallback | aios.py | 931-954 |

---

## Commands At a Glance

### Portfolio Operations
```bash
aio all c:2              # 2 Codex per project
aio all c:1 l:1 g:1      # Mixed agents
aio all c:2 --seq        # Sequential execution
```

### Single Project Operations
```bash
aio multi 0 c:3 "task"   # 3 Codex in project 0
```

### Git Operations
```bash
aio push "message"       # Commit and push
aio pull -y              # Force pull (destructive)
aio setup <url>          # Initialize repo
```

### Worktree Management
```bash
aio w                    # List
aio w0                   # Open worktree 0
aio w++ 0 "msg"          # Remove and push
```

---

## File Organization in Repository

```
/home/seanpatten/projects/aios/
├── aio.py                           # Main script (2,432 lines)
├── aios.py                          # Legacy version (1,410 lines)
├── README.md                        # Original documentation
├── CHANGELOG.md                     # Version history
├── INDEX.md                         # This file
├── EXPLORATION_SUMMARY.md           # Executive summary
├── AIOS_PROJECT_ANALYSIS.md         # Complete technical analysis
├── QUICK_REFERENCE.md               # Command reference
├── GIT_AUTH_DEEP_DIVE.md            # Authentication deep-dive
├── data/
│   ├── config.json                  # Auto-update config
│   ├── aio.db                       # SQLite database
│   └── timings.json                 # Performance baselines
├── jobs/                            # Job execution directories
└── tasks/                           # Task definition files
```

---

## How to Use These Documents

### For New Users
1. Start: QUICK_REFERENCE.md (first 50 lines)
2. Then: EXPLORATION_SUMMARY.md (Section 2-3)
3. Practice: QUICK_REFERENCE.md (Commands section)

### For Developers
1. Start: EXPLORATION_SUMMARY.md (Section 5-6)
2. Deep-dive: AIOS_PROJECT_ANALYSIS.md (Sections 2-5)
3. Specific topics: Search by section

### For Understanding Git Auth
1. Overview: EXPLORATION_SUMMARY.md (Section 4)
2. Details: GIT_AUTH_DEEP_DIVE.md
3. Code: AIOS_PROJECT_ANALYSIS.md (Section 4)

### For Troubleshooting
1. Quick tips: QUICK_REFERENCE.md (Troubleshooting)
2. Auth issues: GIT_AUTH_DEEP_DIVE.md (Section 5)
3. Code locations: EXPLORATION_SUMMARY.md (Code Locations)

---

## Document Statistics

| Document | Lines | Size | Purpose |
|----------|-------|------|---------|
| AIOS_PROJECT_ANALYSIS.md | 607 | 18K | Complete technical reference |
| EXPLORATION_SUMMARY.md | 391 | 12K | Executive summary |
| GIT_AUTH_DEEP_DIVE.md | 508 | 14K | Authentication deep-dive |
| QUICK_REFERENCE.md | 309 | 7.1K | Command reference |
| **Total** | **1,815** | **51K** | **Complete documentation** |

---

## Key Insights

### 1. Multi-Level Architecture
- **User Level:** Command-line interface (aio command)
- **Session Level:** tmux sessions and worktrees
- **Project Level:** SQLite database with config/projects/sessions
- **System Level:** Git integration with HTTPS fallback

### 2. Authentication Innovation
- Non-interactive environment variables for background operations
- SSH-to-HTTPS automatic fallback (unique feature)
- Credential caching (1 week, memory-only)
- Timeout protection (prevents hanging)

### 3. Scalability
- Single project: `aio multi c:3 "task"` (N × 1)
- All projects: `aio all c:3 "task"` (N × M)
- Sequential mode: `--seq` flag for resource constraints

### 4. Reliability
- Fast-forward only updates (safe)
- Non-blocking operations (background threads)
- Silent failure (never blocks startup)
- Database persistence (SQLite with WAL)

---

## Related Files in Project

- **aio.py** (2,432 lines) - Main implementation
- **aios.py** (1,410 lines) - Legacy implementation
- **README.md** - Original documentation
- **CHANGELOG.md** - Version history

---

## Contact & Maintenance

These documents were generated as part of comprehensive AIOS project exploration:
- Date: November 1, 2025
- Scope: Complete codebase analysis
- Coverage: Architecture, commands, authentication, operations

---

## Next Steps

1. **Review** the appropriate document for your use case
2. **Reference** code locations in AIOS_PROJECT_ANALYSIS.md for implementation details
3. **Consult** QUICK_REFERENCE.md for command syntax
4. **Debug** using GIT_AUTH_DEEP_DIVE.md for authentication issues

---

**End of Documentation Index**

