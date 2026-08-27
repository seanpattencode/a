#!/bin/sh
# a agent labels — the super-simple agent↔problem registry (Sean 2026-08-05).
#   sh ~/a/lib/label.sh [board]              board: LABEL · WINDOW · LIVE (web twin on the i box: localhost:9999/problems)
#   sh ~/a/lib/label.sh <label> <j-name>     label an agent: renames its tmux window to <label> + registry row
# Label IS the tmux window name (visible in `a i` picker); registry = adata/git/labels.txt (synced), last row per label wins.
# Protocol that uses this: ~/i/how/problem-agents.txt
R="$HOME/a/adata/git/labels.txt"
if [ -z "$1" ] || [ "$1" = board ]; then
    printf '%-18s %-14s %s\n' LABEL WINDOW LIVE
    [ -f "$R" ] && tac "$R" | awk -F'\t' '!s[$1]++' | tac | while IFS='	' read -r l w d; do
        live=$(tmux list-windows -a -F '#{window_name}' 2>/dev/null | grep -cx -- "$l")
        printf '%-18s %-14s %s\n' "$l" "$w" "$([ "${live:-0}" -gt 0 ] && echo live || echo GONE)"
    done; exit 0
fi
T=$(tmux list-windows -a -F '#{session_name}:#{window_index} #{window_name}' 2>/dev/null | awk -v n="$2" '$2==n{print $1;exit}')
[ -n "$T" ] && tmux rename-window -t "$T" "$1" || { echo "x window '$2' not found"; exit 1; }
printf '%s\t%s\t%s\n' "$1" "$2" "$(date +%F)" >> "$R"
echo "labeled: $1 ← $2 ($T)"
