# a

AI agent manager. Human-AI interaction accelerator. Personal device computer fleet operator. 

## Install

```
curl -fsSL raw.githubusercontent.com/seanpattencode/a/main/a.c | sh
```

**Windows:** Install WSL first (`wsl --install -d Ubuntu` in PowerShell as admin, restart), then run above in Ubuntu.

**Termux:** `pkg install git curl -y` first, then run above.

## Simple start

```
a tutorial
```

Guided walkthrough — tells you what `a` is, asks what you're working on, and teaches commands as you go. You'll accomplish real work on your project while learning.

## Core Commands

```bash
a                # Show all commands 
a c              # Start Claude (co=codex, g=gemini, a=default agent)
a push           # Checkpoint: commit + push
a pull           # Nuke local: reset to remote
a diff           # Check token diff
a revert         # Interactive: pick commit to restore
a <#>            # cd to project by number
a j "prompt"     # Launch agent job in background
a n "text"       # Quick note
a hub            # Schedule operations
a help           # Full command list
```

## Multi-device

Auth syncs across devices:
- First device: `gh auth login` then `a login save`
- Additional devices: `a login apply` (imports token from sync)

## Evolve it

1. Start existing project
2. Hit friction
3. Ask agent to fix a
4. Now a handles that
5. Repeat

Simple code — you can read all of it. Fork it, change it, make it yours. PRs are merged very fast worst case one day.

## Depth

Full framework: [IDEAS.md](IDEAS.md) — explains purpose and philosophy of the project in ranked points.
