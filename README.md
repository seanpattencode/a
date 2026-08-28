# a

> "The system we want to improve can thus be visualized as a trained human being together with his artifacts, language, and methodology."
>
> "…a way of life in an integrated domain where hunches, cut-and-try, intangibles, and the human 'feel for a situation' usefully co-exist with powerful concepts, streamlined terminology and notation, sophisticated methods, and high-powered electronic aids."
> — Douglas Engelbart, *Augmenting Human Intellect*, 1962

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
a                # Menu of everything — type to filter, enter to run
a c              # New tmux window running Claude (co=codex, g=gemini, a=default)
a j "prompt"     # Job: own worktree + agent, runs in the background
a <#>            # cd to project by number
a f              # Triage: notes/tasks/prompts → agents
a n "text"       # Write a note
a push           # Checkpoint: commit + push
a diff           # Token diff vs main (also the gate `a push` enforces)
a pull           # Nuke local: hard reset to remote, delete untracked
a revert         # Pick a commit to restore
a cat            # Whole codebase as text, to paste into an LLM
a ssh <host>     # Shell on another machine (`a ssh all` = a window per host)
a hub            # Schedule jobs: add / run / rm / log
a help           # Full command list
```

Every row in the `a` menu carries a short grey description of what it does. For
script commands it is read from that script's own first line, so it stays true as
they change; hosts show their address, projects their path.

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
