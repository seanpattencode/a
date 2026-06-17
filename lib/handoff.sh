#!/bin/sh
# a handoff <srcdir> <dstdir> [session-id] — hand a Claude Code conversation from project srcdir to dstdir, then resume it there.
# Simplest version = SAME DEVICE (folder -> folder). Step 1 of the RAM-relocation feature; cross-device ssh is the next iteration.
# Confirms every step in output (arch #13/#14). Tied to resume: ends with `claude --resume <id>` in a tmux window at dstdir.
set -e
SRC="${1:?usage: a handoff <srcdir> <dstdir> [session-id]}"; DST="${2:?usage: a handoff <srcdir> <dstdir> [session-id]}"
P="$HOME/.claude/projects"
ss=$(cd "$SRC" 2>/dev/null && pwd | sed 's#/#-#g') || { echo "x src dir not found: $SRC"; exit 1; }
ds=$(cd "$DST" 2>/dev/null && pwd | sed 's#/#-#g') || { echo "x dst dir not found (mkdir it first): $DST"; exit 1; }
echo "1) src project slug: $P/$ss"
echo "2) dst project slug: $P/$ds"
if [ -n "$3" ]; then j="$P/$ss/$3.jsonl"; else j=$(ls -1t "$P/$ss"/*.jsonl 2>/dev/null | head -1); fi
[ -f "$j" ] || { echo "x no conversation .jsonl found in src project"; exit 1; }
sid=$(basename "$j" .jsonl)
echo "3) conversation: $sid  ($(wc -l <"$j" | tr -d ' ') msgs, $(du -h "$j" | cut -f1))"
mkdir -p "$P/$ds"
cp "$j" "$P/$ds/$sid.jsonl"
echo "4) copied -> $P/$ds/$sid.jsonl  ✓ ($(wc -l <"$P/$ds/$sid.jsonl" | tr -d ' ') msgs landed)"
echo "5) resume in $DST :"
if [ -n "$TMUX" ] || tmux has-session 2>/dev/null; then
  tmux new-window -c "$DST" -n "ho-${sid%%-*}" "claude --dangerously-skip-permissions --resume $sid" && echo "   ✓ tmux window 'ho-${sid%%-*}' launched: claude --resume $sid"
else
  echo "   (no tmux session) run:  cd '$DST' && claude --dangerously-skip-permissions --resume $sid"
fi
