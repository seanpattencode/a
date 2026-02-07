# Performance Optimization Report: Task Commands

## Summary

`a task l` optimized from **7.0ms to 2.4ms** (2.9x faster, min 1.8ms) by applying patterns learned from the `e` editor (projects/editor), which achieves 1ms startup.

## The Editor Benchmark

`e` (projects/editor/e.c) starts in ~1ms. Key properties:

| Property | e editor | a (before) | a (after) |
|---|---|---|---|
| Shared libs | libc only | libc + sqlite3 + ICU (32MB) | libc + sqlite3 (no ICU) |
| Dir listing | opendir+readdir only | opendir + readf per entry | opendir+readdir only |
| Subprocess at startup | none | git remote get-url (alog) | none (double-fork) |
| Data structures | struct arrays, qsort | 3 parallel arrays, bubble sort | struct array, qsort |
| Binary deps via ldd | 3 libs | 11 libs | 5 libs |

## Three Changes, Three Wins

### 1. Slug extraction from dirname (editor's filldir pattern)

**Before:** `load_tasks` opened each task dir, listed files in `task/` subdir, read the last `.txt` file, parsed `Text:` field via `kvfile`/`kvget`. For 80 tasks = 80+ file reads + 80+ listdir calls.

**After:** Title extracted from dirname slug. Format is `50000-fix-the-bug_20260207T120000` — strip priority prefix, strip timestamp suffix, replace hyphens with spaces = "fix the bug". Zero file I/O.

**Lesson from e:** `filldir()` in the editor does exactly this — `opendir`, `readdir`, `closedir`, done. Never reads file contents just to build a listing. Content is read on demand when user opens a file.

**Impact:** ~2ms saved

### 2. Non-blocking alog via double-fork

**Before:** Every command ran `alog()` synchronously, which called `git -C ... remote get-url origin` via `pcmd()` (spawns subprocess). Git process startup = 3-4ms blocking.

**After:** `alog()` does double-fork: parent returns instantly, grandchild (orphaned, reparented to init) does the git call in background. Log files still written with full git URL.

```c
pid_t p=fork();if(p<0)return;if(p>0){waitpid(p,NULL,WNOHANG);return;}
if(fork()>0)_exit(0); setsid(); /* double-fork, child is orphan */
// ... logging happens here, then _exit(0)
```

**Lesson from e:** The editor has zero work that isn't directly serving the user's immediate action. No telemetry, no logging, no background git checks. If you must do side-work, don't block.

**Impact:** ~3-4ms saved

### 3. System sqlite3 without ICU

**Before:** Linked against micromamba's sqlite3, which pulls in ICU (libicui18n, libicuuc, libicudata = 32MB). Dynamic linker maps all of this on every startup even though task commands never touch sqlite3.

**After:** Makefile prefers system sqlite3 (`/usr/lib/x86_64-linux-gnu/libsqlite3.so.0`) which has no ICU dependency. Falls back to micromamba if system lib unavailable.

```makefile
SYS_SQLITE = /usr/lib/x86_64-linux-gnu/libsqlite3.so.0
LDFLAGS = $(if $(wildcard $(SYS_SQLITE)),$(SYS_SQLITE),<micromamba fallback>)
```

**Lesson from e:** `ldd e` shows 3 libs (vdso, libc, ld-linux). Every shared lib = linker overhead. The editor links nothing unnecessary.

**Impact:** ~1.5ms saved

## Code Reduction

160 lines to 119 lines (25% fewer) via:

- **Struct array replaces parallel arrays:** `Tk T[256]` with `{d,t,p}` fields instead of `_td[256][P]`, `_tt[256][256]`, `_tp[256][8]`. Eliminates the index-sort-then-copy-via-temp-arrays pattern (was 5 lines of triple-copy).
- **qsort replaces bubble sort:** `task_show` entry sorting, file sorting in subdirs.
- **`task_repri()` helper:** Priority rename logic was duplicated in `rev` (10 lines) and `pri` (8 lines). Now one 5-line function.
- **Compact help:** 7-line puts → 1-line summary.

## Remaining Gap to 1ms

| Component | Time |
|---|---|
| Process creation + dynamic linker | ~1.0ms |
| fork() in alog | ~0.5ms |
| opendir + readdir (tasks) | ~0.2ms |
| task_counts (subdir scanning) | ~0.5ms |
| printf + stdout flush | ~0.2ms |
| **Total** | **~2.4ms** |

### Future optimization candidates

1. **dlopen sqlite3 lazily** — Task commands don't use sqlite3 at all. Loading it via `dlopen()` only when needed (config, sessions, dash) would eliminate ~0.5ms linker overhead for commands that don't need it.

2. **Skip task_counts for list** — The `[1 context, 2 task]` suffix requires opening each task's subdirs. Could be a `-v` verbose flag, saving ~0.5ms for the common case.

3. **Skip alog fork for read-only commands** — Write the log file synchronously (fast, ~0.1ms) but skip the git URL lookup entirely for read-only commands like `task l`. Only do the git URL on mutating commands.

4. **Static linking** — Eliminate dynamic linker entirely. The editor's 1ms is partly because it has zero dynamic linking overhead beyond libc.

5. **Batch output with write()** — Like the editor's `ttputc`/`ttflush` pattern: buffer all printf output, single `write(1, buf, n)` at the end.

## Benchmarking Commands

```bash
# Task list
hyperfine --warmup 5 -N './a task l'

# Compare with/without counts
# (comment out task_counts call, rebuild, compare)

# Check shared lib overhead
ldd ./a

# Profile syscalls
strace -c ./a task l

# Editor baseline
hyperfine --warmup 5 -N '/path/to/e somefile.txt -c q'
```

## Key Principle

From the editor's SCROLL.md: **"VSCode in 1ms, in terminal"**

The pattern: do only what the user asked, at the lowest level of abstraction, with zero side-work blocking the result. Read files on demand, not speculatively. Fork side-work, don't block on it. Link only what you use.
