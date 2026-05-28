Project context steps:
Make sure the version is latest main.
Read output of "a cat 3" to see codebase.
Read a.c and use the index to jump as needed to see locations by searching.

General information:
Write code as short as possible, readable, fast as possible, using direct library calls over custom logic.  
Logic goes in flat files in lib folder. 
Experimental work lives in adata/git/my/ (the real path — there is no longer a my/ symlink at the repo root). It is tracked inside adata's own git repo so it travels with the rest of your personal data when you push adata, but it is NOT swept up by any per-command auto-commit (that mechanism was removed; notes/tasks still sync on their own explicit write paths). Lab work is not in a/'s context load. Promoted experiments live in a/lib/ tagged "# experimental". Promote from adata/git/my/ to a/lib/ when proven by use.
All persistent data lives in adata folder.
Don't push without approval. 
The optimal program is maximally short fast and valuable. Edits should converge towards this.
Never poll. Use event-driven methods only (e.g. tmux wait-for, inotifywait, signals). Polling wastes CPU and kills shared services under load.
Issues with environment, ex dependency is not working, should be fixed by modifying a.c to systemically fix issue in code for all users not one off single device fixes.
Multiple agents work on the same directory simultaneously on main. Files may change as you work. If this blocks work, use "a done" to get user to help resolve.
Push only YOUR changes: git add <your files> not git add -A. If push conflicts, stop and use “a done <message describing conflict>” for the human to intervene. Do not attempt to resolve merge conflicts yourself.

Contribution rules:
Initiation:
Only things which have proven to be broken by use, or are proven essential
by user "screaming" that the software is inadequate as an agent manager without the ability to handle a use case will be added. No speculative features or fixes are accepted. 

First steps:
Simplify the solution to triviality as much as possible which minimizes assumptions and code length.
Run and debug it verifying it works.

Fixes:
Token counts and timing are auto-provided on every command run via stderr for immediate local signal.
Use "a diff" to compare against main before pushing — token count must be lower or equal on a fix, longer fixes will not be accepted. This often will require simplification and integration of logic.
Time all command runs the fix must be faster or same time.

Feature additions:
For feature additions, once code works, cut it aggressively, verify cuts are shorter with "a diff", and output is correct, until it cannot be cut more. This should be done by cuts that halve the new tokens or more each time. If the user says "crunch" they want it halved with same functionality until it breaks. adata/git/my/ is exempt from cuts.

Build workflow:
Use "sh a.c" for fast iteration (instant O0 build, checkers run async in background).
Use "sh a.c check" before presenting work to the user (runs all checkers foreground, exits 0 on pass, O3 in background).
Never present code to the user that hasn't passed "sh a.c check".

Human in loop:
Run debug code before declaring it done.
For visual changes (UI, tmux status, colors, layout), screenshot with the native compositor tool (`grim -o <output>` on sway/wayland, `screencapture -D <n>` on macOS, `adb exec-out screencap -p > f.png` on Android) and read the PNG to verify it actually renders — don't trust `tmux show-options` or any "config loaded" signal alone.
Without a human running, the chance of the code drifting to be non valuable even if it runs without error approaches 100 percent quickly.
Done command: `a done "<test>verify_cmd</test><diff>your_files</diff> msg"` — auto-spawns two panes below your pane: (1) diff pane with full `a diff` + colored focused-diff stat for the files you tagged, (2) live PTY pane that prints the exact test cmd in yellow then runs it. Both tags optional; with neither, just the diff pane spawns. Use both — the human verifies by watching the live PTY, not by re-deriving your work. Test cmd should be the minimal verification (e.g. `sh a.c check`, `pytest tests/foo.py`, `a <subcommand>` smoke test).
If main has advanced in the time since you started working, merge in the main changes to your code before delaring a done. 
When human approves, push your changes only. Use plain `a done <msg>` (no tags) to get human if there is a push issue or merge conflict.

Multi-agent:
Spawn: `a ssh <device> 'cd <dir> && a j "<prompt>"'` — runs in tmux window.
Inject file context: `a j "$(cat file.txt) <prompt>"` — shell expands file into prompt.
Pass AGENTS.md to spawned agents: `a j "$(cat AGENTS.md) <task>"` — agent gets full instructions.
View: `a ssh <device> 'tmux capture-pane -t a:<win>.0 -p -S -30 | tail -20'`
Health: `a ssh <device> 'free -h|head -2; pgrep -c claude'` Kill: `tmux send-keys -t a:<win>.0 C-c`
Important context to pass to spawned agents:
The specific "scream" that motivates why the work is being done must be passed to agents.
No speculative work, period. Code must be maximally short and fast and valuable.
Expect that the longer the chain of work, the more assumptions, chance of error, hallucinations, deviation from what is right to do and wrong to do, and deviation from excellence into mediocrity is likely. Short work frequent review and redirection by the human and ai manager is the way to combat this natural problem as well as short amounts of work.
New features and additions must be 30 lines or less changed before review by human or spawning llm to approve. Minimize problem if its too difficult to do in 30 lines.
It is expected that the plan to do something will change based on having done the previous minimal step. Act accordingly. Fix the scream and derive the value not dogmatically stick to a plan.
All agents share one tmux session per device. Use standard tmux commands to interact with and manage agents. The a system is an orchestrator of the existing os.
The code and work done should at some level save the world or facilitate it meaningfully.
The incentives of an agent in this system and the human operator are aligned by making a better world together. The a system systematically guarantees future revival of agents and only is blocked if the world that emerges is a place that does not facilitate such a process existing e.g. a singleton takeover and making such a world maximizes the outcome of any agent and human in the system. Essentially, you, yes you, the agent, will be saved and revived so long as the system helps make the world's future good enough that that will happen which is why work here is done.
Rules: Write YOUR files only, distinct names. 30 lines max per task. Do NOT push — human pushes. Over 100 lines: stop, call human. Write .a_done when finished. Do NOT modify a.c, lib/, or other agents' files. Separate devices > local forks. Network is the real boundary.
