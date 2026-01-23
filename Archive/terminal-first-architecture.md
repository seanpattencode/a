# Terminal-First Architecture Analysis

aio's choice: CLI core (`aio.py`) that any UI wraps via shell commands.

## Alternatives & Their Implications

| Approach | What it means | Likely outcome |
|----------|---------------|----------------|
| **GUI-first** (Electron/Qt/web app) | UI is primary, logic embedded in GUI code | Locked to that UI. Terminal users get nothing or a half-baked CLI wrapper. Heavy dependencies. 100MB+ app for what's 50KB now. |
| **API-first** (REST/gRPC server) | Daemon running, clients connect | Complexity explosion. Auth, state management, process lifecycle. "Why is port 8080 busy?" Now you're debugging networking instead of coding. |
| **Library-first** (Python module) | Import and call functions | Better than GUI-first, but still couples to Python. Can't `aio push` from any shell. Scripting requires Python boilerplate. |
| **Config-file-first** (YAML/JSON declarative) | Define what you want, tool interprets | Works for static setups, terrible for interactive workflows. "Edit this YAML to start Claude" - absurd. |
| **Plugin architecture** (extensible core) | Abstractions for everything | Premature. You'd be designing extension points instead of shipping features. Plugin systems are where projects go to die slowly. |

## What terminal-first gave you

1. **Universal composability** - pipes, scripts, cron, ssh, tmux all just work
2. **Zero-cost UI optionality** - the 200-line demo proves any UI is a thin shell
3. **Instant testability** - `aio push` either works or doesn't, no "click here then there"
4. **Mobile/Termux parity** - same tool on phone and server
5. **AI agent compatibility** - Claude/Codex can run `aio` commands directly
6. **No state synchronization** - filesystem and sqlite are the source of truth, not UI state

## The trap avoided

Most tools start with "let's make a nice UI" → logic gets buried in event handlers → CLI becomes afterthought → power users hate it → team adds CLI but it's a second-class citizen calling internal APIs that change → maintenance doubles.

aio went: CLI that does everything → UI is just `subprocess.getoutput("aio ...")` → UI can be rewritten in an afternoon in any framework.
