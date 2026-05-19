#!/bin/sh
# Per-window omnibox dock: spawn/focus 8-line `a i` pane at bottom; toggle out if inside.
W=$(tmux display -p '#{window_id}'); C=$(tmux display -p '#{pane_id}')
D=$(tmux list-panes -t "$W" -F '#{pane_id}|#{pane_start_command}'|awk -F'|' '/while a i/{print $1;exit}')
[ -z "$D" ] && exec tmux split-window -t "$W" -fv -l 8 -P -F '#{pane_id}' 'while a i;do :;done'
[ "$C" = "$D" ] && exec tmux last-pane
exec tmux select-pane -t "$D"
