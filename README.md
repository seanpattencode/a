# AIOS Task Manager

A professional terminal UI for running multi-step jobs in parallel with live status updates. Perfect for managing codex workflows, build pipelines, or any multi-step automation.

## Features

✅ **Stable Input** - Type freely while status updates above (never disappears!)
✅ **Live Status** - Real-time job progress updates every 0.5 seconds
✅ **Interactive Builder** - Build multi-step jobs on the fly
✅ **Parallel Execution** - Run up to 4 jobs simultaneously
✅ **Isolated Sessions** - Each job runs in its own tmux session
✅ **Auto Cleanup** - Keeps last 20 job directories, removes older ones
✅ **Organized Output** - All job output in `jobs/` directory

## Installation

```bash
pip install prompt_toolkit libtmux
```

## Quick Start

### Start the Manager

```bash
python python.py
```

You'll see a full-screen terminal UI:

```
================================================================================
AIOS Task Manager - Live Status
================================================================================

Running Jobs:
--------------------------------------------------------------------------------
  (no running jobs)

Task Builder:
--------------------------------------------------------------------------------
  (no tasks being built)

================================================================================
Commands: <job>: <desc> | <cmd>  |  run <job>  |  clear <job>  |  quit
================================================================================

❯ _
```

### Build and Run a Job

**Add steps to a job:**
```
❯ demo: Create file | echo 'Hello World' > test.txt
❯ demo: Show content | cat test.txt
❯ demo: List files | ls -la
```

The Task Builder section updates to show your job being constructed.

**Run the job:**
```
❯ run demo
```

Watch it progress through steps in real-time! The status changes from:
- ⟳ Running (yellow) → ✓ Done (green)

**Exit when done:**
```
❯ quit
```

Or press `Ctrl+C`

## Command Reference

### Build Jobs

```
<job-name>: <description> | <command>
```

Examples:
```
build: Compile code | make
build: Run tests | make test
build: Deploy | make deploy
```

### Execute Jobs

```
run <job-name>
```

Example:
```
❯ run build
```

### Clear Jobs

```
clear <job-name>
```

Removes a job from the builder without running it.

### Quit

```
quit
```

Or press `Ctrl+C`

## Real-World Examples

### Example 1: Codex Workflow

```
❯ factor: Create program | codex exec --model gpt-5-codex -- "create factor.py that factors numbers"
❯ factor: Test with 84 | python factor.py 84
❯ factor: Test with 100 | python factor.py 100
❯ run factor
```

### Example 2: Build Pipeline

```
❯ deploy: Run tests | pytest tests/
❯ deploy: Build Docker | docker build -t myapp:latest .
❯ deploy: Push image | docker push myapp:latest
❯ deploy: Update k8s | kubectl apply -f k8s/
❯ run deploy
```

### Example 3: Parallel Jobs

```
❯ job1: Task A | sleep 5 && echo 'Job 1 done'
❯ job2: Task B | sleep 3 && echo 'Job 2 done'
❯ job3: Task C | sleep 4 && echo 'Job 3 done'
❯ run job1
❯ run job2
❯ run job3
```

All three run simultaneously!

## Load from JSON Files

Create a task file `deploy.json`:

```json
{
  "name": "deploy-staging",
  "steps": [
    {"desc": "Run tests", "cmd": "pytest tests/"},
    {"desc": "Build Docker image", "cmd": "docker build -t api:latest ."},
    {"desc": "Push to registry", "cmd": "docker push api:latest"},
    {"desc": "Deploy to k8s", "cmd": "kubectl apply -f k8s/staging/"}
  ]
}
```

Load and run:

```bash
python python.py deploy.json
```

Or load multiple tasks:

```bash
python python.py deploy-staging.json deploy-prod.json deploy-eu.json
```

All will queue and run in parallel!

## Job Output

### Output Location

All job output goes to `jobs/` directory:

```
jobs/
├── demo-20251003_203045/      # First run of 'demo' job
│   ├── test.txt
│   └── ...
├── demo-20251003_203152/      # Second run of 'demo' job
│   └── ...
└── deploy-20251003_203301/    # 'deploy' job
    └── ...
```

### Auto Cleanup

Automatically keeps the last 20 job directories and removes older ones after each job completes.

### View Job Output

```bash
# List all job outputs
ls jobs/

# View specific job output
cd jobs/demo-20251003_203045/
ls -la
```

## UI Layout

```
┌──────────────────────────────────────────┐
│  Status Display                          │  ← Updates every 0.5s
│  ┌────────────────────────────────────┐  │
│  │ Running Jobs:                      │  │
│  │   job1  | 2/3: Build  | ⟳ Running │  │
│  │   job2  | Done        | ✓ Done    │  │
│  │                                    │  │
│  │ Task Builder:                      │  │
│  │   job3:                            │  │
│  │     1. Create file                 │  │
│  │     2. Test file                   │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│  Output Messages                         │  ← Command feedback
│  ✓ Queued job: demo                      │
├──────────────────────────────────────────┤
│  ❯ your command here_                    │  ← Always stable
└──────────────────────────────────────────┘
```

## Status Colors

- **Yellow** (⟳ Running) - Job is executing
- **Green** (✓ Done) - Job completed successfully
- **Red** (✗ Error/Timeout) - Job failed

## Technical Details

### Architecture

- **prompt_toolkit** - Professional terminal UI (same library as IPython, pgcli)
- **libtmux** - Tmux session management
- **Threading** - 4 worker threads for parallel execution
- **Thread-safe** - Locks protect shared state

### Requirements

- Python 3.7+
- Real terminal (TTY) - won't work via pipes or background
- tmux installed

### Configuration

Edit these constants in `python.py`:

```python
MAX_JOB_DIRS = 20    # Number of job directories to keep
```

Worker count (line ~271):
```python
workers = [Thread(target=worker, daemon=True) for _ in range(4)]  # 4 workers
```

Step timeout (line ~168):
```python
if not wait_ready(session_name, timeout=120):  # 120 seconds
```

## Troubleshooting

### "Input is not a terminal" error

You're running without a TTY (e.g., via pipe or background).

**Solution:** Run directly in a terminal:
```bash
python python.py
```

Not via:
```bash
python python.py < commands.txt  # ✗
python python.py &               # ✗
echo "quit" | python python.py   # ✗
```

### Jobs stuck in "Running"

Check tmux sessions:
```bash
tmux ls | grep aios
```

Attach to a job's session:
```bash
tmux attach -t aios-demo-20251003_203045
```

Kill stuck session:
```bash
tmux kill-session -t aios-demo-20251003_203045
```

### UI looks broken

Ensure:
- Terminal is at least 80 characters wide
- Terminal supports 256 colors
- Using a modern terminal emulator

## Examples Included

- `test_task.json` - Simple 2-step example
- `example_task.json` - Codex factorization workflow
- `parallel_task.json` - Sorting algorithm example

Try them:
```bash
python python.py test_task.json
```

## Similar Tools

This UI style is similar to:
- **IPython** - Interactive Python shell
- **k9s** - Kubernetes TUI
- **lazygit** - Git TUI
- **pgcli** - Postgres client
- **mycli** - MySQL client

All use prompt_toolkit for stable, professional terminal UIs.

## Why This Works

Previous approaches (rich.Live, ANSI codes) refreshed the screen and disrupted input.

**prompt_toolkit** solves this by:
1. Properly abstracting terminal handling
2. Separating UI components (status, output, input)
3. Event-driven updates that don't touch input
4. Professional-grade terminal management

Your input stays **rock solid** while status updates above!

## License

Open source - use freely for your projects.

## Credits

Built with:
- [prompt_toolkit](https://github.com/prompt-toolkit/python-prompt-toolkit) - Terminal UI framework
- [libtmux](https://github.com/tmux-python/libtmux) - Tmux automation
