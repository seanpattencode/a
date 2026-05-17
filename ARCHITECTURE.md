## Principles
1. As a general principle, tools must have an interface that works well for both LLM agents and humans. This is crucial for alignment and collaborative human-AI work because it gives a shared function set and information flow to the user and LLM.
2. Speed for the user is essential, as is minimizing keystrokes and input actions to accomplish anything. Especially mobile users need chars minimized at all cost.
3. There should be no polling, only event-driven processes.
4. OS program dependencies and software dependencies must all be handled by the script, not ad hoc install per machine. If the script fails to work on a new machine because of a missing dependency, it's the script's fault for not auto-installing it properly, not the user's fault.
5. Exceeding the time limit should kill the program, and lack of completion is an error to fix by faster software.
6. Scripts should expect to die and save persistent data to adata given that expectation.

## Organization
7. All persistent data goes in adata, period. adata/git will hold user personal git-synced data.
8. All logic goes to /lib. /lib files should be as independent as can be from one another, but some shared logic is inevitable. Folders only when external tooling forces it (Chrome extension, Python package) AND files are useless apart; categorical grouping is not sufficient — flat names with prefixes replace category folders.
9. a.c handles build, dep install, and dispatch to /lib.

## Rules
10. Software is killed at 1 sec by default, and limits are auto-tightened continuously by best time of past through a perf. Network-dependent operations may be 5 sec at max.
11. If software must operate indefinitely, like waiting for user input or running a server, it must reach a no-op, no-mem-change state before disarming the timer.
12. No command or operation should ever be exempt from time killing except the no-op state and necessary dep downloads. Download the highest-priority deps first. The number of deps and time to install must be minimized.
13. First-time installation, compiling, and installing dependencies are also program operations under time limits, with a dep download time exception.
14. Use C unless unsuitable.
15. All commands and subcommands must be accessible in the a i tui.
16. Commands should follow the format: 3 chars or less, occasionally 4, never more; same name as file unless file handles many cmds.
17. A cmd with no parameter should show the menu of commands, how to type them, and the obvious most common information the user wants.
18. All sub cmds must be one char to call. cmd <text> should do the obvious thing when given text the user wants most often.
19. Show all subcommands exactly as you would type them to call them, and explain what they do in 4 words or less.

## Jobs
20. Every long-running job runs as a tmux window in the single shared `a:` session — never in a tool-private background. Humans and LLMs use the same primitives (tmux attach, tmux capture-pane, tmux send-keys) to observe and control the same process. A job invisible to one party is a violation: if the LLM can see it but the user cannot, or vice versa, oversight and collaboration break down.
21. Window naming is predictable: `<cmd>-HHMM` so multiple runs of the same command coexist and are timestamped at a glance. Every window tees its output to `adata/local/bg/<name>.log` so tailing, grepping, and monitor-style event streams all work without polling the pane. An opening banner prints start time, full command, and log path before the job runs, so attaching always shows what is happening.
22. Control is universal across actors: `tmux send-keys -t a:<name> C-c` for clean interrupt, `tmux kill-window` for hard kill, `tmux attach` for live view, `a ls` for listing. No tool needs a private job manager — tmux is the job manager. Claude Code's `run_in_background`, spawned agents, and user shells all converge on the same windows, enabling cross-agent visibility and making every job reviewable, interruptible, and resumable by anyone on the device.

## Pty & Iteration
23. Long running tasks and parallel work should be done as pty sessions. The atomic unit of a computational job is pty session plus program, not the bare process. This is a practical necessity given programs inevitably need interactivity and produce errors that require observation of their working state.
24. More time means more error chance. Interactivity is what allows fixing those errors. Standard input/output interactivity needs are universal, and the pty solves them generically — you do not need to invent an I/O format per program.
25. Programs should generally be self-responsible for saving incremental work and restarting gracefully. The script owns its own durable state; the orchestrator only launches and kills.
26. For a distributed system, killing jobs and repurposing should be doable in milliseconds. Killing all ptys and launching new ones does this natively. Traditional systems built to manage this are overhead for no reason and only harm latency.
27. In the real world, latency to change beats throughput, because it minimizes time to iterate. Improving the process matters more than doing the wrong or worse thing more times.
28. Worst case, drop everything and pivot — this is necessary to minimize time to correct when a direction turns out wrong.
29. The system must be structured so that a llm with limited context window and human can easily understand the system and full codebase in context still works for llms. a.c is the single file that if nothing else viewed should explain the project. This might make some info redundant to a whole repo reader but its worth it. 
30. Trust and correctness comes from the user being able to use tools and artifacta to verify items and work directly. Not, the work is done human, but, here is the code here are commands to use in terminal to verify the actual thing is passed to user. 

## The m meta agent/persistent agent

31. The e editor handles the user interaction and editing of the meta agent and more broadly serves as its ui layer. Changes to editor will be made to support changes to meta agent ui as needed. This is neccesary because a text editor is essential for making the agent editable by human trivially and e is a text editor that the main developer, Sean Patten, can control and edit and is sufficently fast for the task. Failures to do UI capabilities or bugs are resolved by fixing the e editor.


