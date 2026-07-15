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

## Show what matters

A system that helps you succeed must make the numbers that decide success **obvious, accurate, and timely** — logged honestly, displayed where you can't miss them. The web home page (`a serve`) surfaces these first, not buried in a submenu.

**Concrete goal — net worth (step 0 of making money):** the home UI shows total net worth prominently at the top, with how stale it is and a one-line box to update it. It is *not recomputed* — it proxies the single source (the `u` trading system's one net-worth formatter), so the figure on the main page and in `/u` can never disagree. Knowing exactly what you have, as timely as possible, is the precondition for growing it.

**The general principle:** log accurately, and make obvious the key statistics whose *reading* most improves your accomplishing the broader goal — enhancing the wellbeing of all sentient life. Instrumentation is not decoration; the metric you see every day is the one you move. Add the stat that would most change your next decision; show it honestly, staleness and all.

## Depth

Full framework: [IDEAS.md](IDEAS.md) — explains purpose and philosophy of the project in ranked points.
