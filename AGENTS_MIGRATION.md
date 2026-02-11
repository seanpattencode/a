# Agents Migration Plan

## Goal

Merge hub (scheduling) and agents (LLM scripts) into one self-contained system.
An LLM looking at the `agents/` folder sees everything: scripts, scheduling, logging, conversations.

## Current state

```
~/projects/a/                              # Code repo
  lib/a_cmd/hub.py                         # Scheduler (115 lines)
  feature_tests/agents/                    # 22 agent scripts (509 lines total)
    agent_base.py                          # Shared: send(), ask_claude() (32 lines)
    gemini_weather_email.py                # Standalone, inlines send/creds (34 lines)
    gemini_claude_collab.py                # Imports agent_base.send (54 lines)
    email.db                               # Credentials (local, untracked)
    ...

~/projects/adata/git/                      # Data repo (git-synced)
  hub/                                     # 17 job definitions (RFC 5322 .txt)
    g-weather.txt                          # Name/Schedule/Prompt/Device/Enabled/Last-Run
    testjob2.txt
    ...
  activity/                                # Per-command one-liners
```

Problems:
- Agent scripts live in "feature_tests" but run in production daily
- Conversation output goes to local hub.log only, not synced
- Logging split between hub.py (execution) and agents (nothing)
- Hub handles both scheduling AND conversation saving — mixed concerns

## Target state

```
~/projects/a/                              # Code repo
  agents/                                  # Everything agent/scheduling related
    base.py                                # Shared: save(), send(), ask_gemini(), ask_claude()
    hub.py                                 # Scheduling: timers, run, add/rm/on/off
    weather.py                             # Was gemini_weather_email.py
    sp500.py                               # Was gemini_sp500.py
    collab.py                              # Was gemini_claude_collab.py
    ...                                    # All 15 agent scripts
  lib/a_cmd/hub.py                         # Thin shim: imports agents.hub.run

~/projects/adata/git/                      # Data repo (git-synced)
  agents/                                  # Replaces hub/ — jobs + conversations
    g-weather.txt                          # Job definition (no timestamp = config)
    g-weather_20260206T073116_HSU.txt      # Conversation output (timestamp = run)
    gc-collab_20260206T090606_HSU.txt      # Multi-turn conversation
    testjob2.txt                           # Non-agent job — lives here too
    sync.txt                               # Non-agent job
  activity/                                # Unchanged — per-command one-liners
```

Convention: no timestamp in filename = job config, timestamp in filename = run output.

## Steps

### Phase 1: Create agents/ directory and move scripts

1. `mkdir ~/projects/a/agents/`
2. Move and rename scripts from `feature_tests/agents/` to `agents/`:
   - `agent_base.py` -> `base.py`
   - `gemini_weather_email.py` -> `weather.py`
   - `gemini_sp500.py` -> `sp500.py`
   - `gemini_claude_collab.py` -> `collab.py`
   - `gemini_claude_notes.py` -> `notes.py`
   - `gemini_claude_worktree.py` -> `worktree.py`
   - `gemini_huang_quote.py` -> `huang.py`
   - `gemini_joke_judge.py` -> `joke.py`
   - `gemini_motivate.py` -> `motivate.py`
   - `gemini_aqi_nyc.py` -> `aqi.py`
   - `gemini_frontier_check.py` -> `frontier.py`
   - `gemini_demis_check.py` -> `demis.py`
   - `gemini_androidtv_check.py` -> `androidtv.py`
   - `gemini_agent_managers.py` -> `managers.py`
   - `gemini_agent_research.py` -> `research.py`
   - `gemini_bot_reminder.py` -> `reminder.py`
   - `gemini_weather2_email.py`, `gemini_weather2.py`, `gemini_weather.py` -> archive or delete (superseded)
   - `agent_code_fix.py`, `agent_goal_step.py`, `agent_motivate.py` -> archive (old agents, not in hub)
3. Move `email.db` to `agents/`
4. Keep `feature_tests/agents/` as-is until verified, then delete

### Phase 2: Update base.py with save() and logging

Add to `base.py`:
- `AGENTS_DIR`: points to `adata/git/agents/`
- `save(name, output)`: writes conversation to `AGENTS_DIR/{name}_{timestamp}_{device}.txt`
  with header (Agent, Date, Device) + full output
- `alog()` call for activity feed entry

```python
# base.py additions
AGENTS_DIR = Path(SCRIPT_DIR).parent.parent / 'adata' / 'git' / 'agents'

def save(name, output):
    """Save agent conversation to git-synced agents dir"""
    AGENTS_DIR.mkdir(parents=True, exist_ok=True)
    now = datetime.now()
    ts = now.strftime('%Y%m%dT%H%M%S')
    fn = f'{name}_{ts}_{DEVICE_ID}.txt'
    header = f'Agent: {name}\nDate: {now:%Y-%m-%d %H:%M}\nDevice: {DEVICE_ID}\n---\n'
    (AGENTS_DIR / fn).write_text(header + output + '\n')
```

### Phase 3: Update agent scripts to use base.save()

Each agent script calls `base.save(name, output)` after producing output.

Example (weather.py):
```python
from base import save, send
# ... run gemini ...
save("g-weather", output)
if "--send" in sys.argv: send("Weather Report", output)
```

Scripts that inline send/get_creds: refactor to import from base.
Scripts that already import from agent_base: update import to `from base import ...`.

### Phase 4: Move hub.py to agents/

1. Copy `lib/a_cmd/hub.py` to `agents/hub.py`
2. Update imports in `agents/hub.py`:
   - `from lib.a_cmd._common import ...` (or use sys.path adjustment)
   - `from lib.a_cmd.sync import sync`
3. Update `HUB_DIR` from `SYNC_ROOT / 'hub'` to `SYNC_ROOT / 'agents'`
4. Update `_install()` ExecStart path: `__file__` is now in `agents/`, so
   `aio` path becomes `{sys.executable} {SDIR}/lib/a.py` (resolve explicitly)
5. Replace `lib/a_cmd/hub.py` with thin shim:
   ```python
   """aio hub - delegates to agents/hub.py"""
   def run():
       import sys; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.dirname(__import__('os').path.dirname(__import__('os').path.abspath(__file__)))))
       from agents.hub import run; run()
   ```
6. In hub.py `run` handler: after running job, call `base.save()` for non-trivial output
   (agents save their own; hub saves for non-agent jobs like testjob2)

### Phase 5: Migrate data (adata/git/hub/ -> adata/git/agents/)

1. `mkdir -p ~/projects/adata/git/agents/`
2. `mv ~/projects/adata/git/hub/*.txt ~/projects/adata/git/agents/`
3. `rmdir ~/projects/adata/git/hub/`
4. `a sync` to commit the move

### Phase 6: Update C binary references

In `a.c`:
- `cmd_sync` folders list: replace `"hub"` with `"agents"`
- No other C changes needed (`cmd_hub` just calls `fallback_py`)

### Phase 7: Update hub job commands

The 17 job `.txt` files in `adata/git/agents/` have Prompt fields like:
```
Prompt: python3 /home/sean/projects/a/feature_tests/agents/gemini_weather_email.py --send
```

Update each to new path:
```
Prompt: python3 /home/sean/projects/a/agents/weather.py --send
```

Then `a hub sync` to regenerate all systemd service files.

### Phase 8: Update sync.py display

In `sync.py`: replace `['hub']` with `['agents']` in the display loop.

### Phase 9: Test

1. `make CC=gcc` - rebuild C binary
2. `a hub` - should show 17 jobs from adata/git/agents/
3. `a hub run testjob2` - should run, save output to adata/git/agents/testjob2_TS.txt
4. `a hub run g-weather` - should run weather.py, save conversation via base.save()
5. `a sync` - should show agents: N files, conversations committed
6. `a log` - activity feed shows agent runs
7. `systemctl --user start aio-testjob2.service` - systemd works
8. `systemctl --user start aio-g-weather.service` - agent works via systemd
9. Verify no old paths referenced: `grep -r 'feature_tests/agents' ~/projects/a/`
10. Verify hub job .txt files all point to new paths

### Phase 10: Cleanup

1. Remove `feature_tests/agents/` (scripts moved to `agents/`)
2. Remove compat references to old hub/ path
3. Commit and push

## Files changed

| File | Change |
|------|--------|
| `agents/` (new dir) | 15 agent scripts + base.py + hub.py |
| `agents/base.py` | Add save(), AGENTS_DIR, DEVICE_ID |
| `agents/hub.py` | Move from lib/a_cmd, update HUB_DIR + imports |
| `lib/a_cmd/hub.py` | Thin shim importing agents.hub |
| `lib/a_cmd/sync.py` | Display loop: hub -> agents |
| `a.c` | Sync folders list: hub -> agents, rebuild |
| `adata/git/agents/` | Move from adata/git/hub/, conversations land here |
| 17 job .txt files | Update Prompt paths |

## Non-goals

- Don't refactor agent script internals (keep them short/simple)
- Don't change the `a hub` CLI interface (still `a hub add/rm/run/sync`)
- Don't change systemd timer naming (still `aio-{name}.timer`)
- Don't add agents to FOLDERS auto-timestamp list (conversations are pre-timestamped by base.save, job configs overwrite in place)
