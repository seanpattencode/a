#!/bin/sh
# Omnibox dock: per-window 8-line `a i` pane stored in @omni_pane. `ensure` creates without focus; default toggles.
o=$(tmux display -p '#{window_id} #{pane_id} #{@omni_pane}')
W=${o%% *};r=${o#* };C=${r%% *};D=${r#* }
{ [ -z "$D" ] || ! tmux lsp -t "$D" >/dev/null 2>&1; } && {
    D=$(tmux split-window -t "$W" -fv -l 8 -d -P -F '#{pane_id}' 'while a i 2>/dev/null;do :;done')
    tmux set -t "$W" -wq @omni_pane "$D"
}
[ "$1" = ensure ] && exit
[ "$C" = "$D" ] && exec tmux last-pane
exec tmux select-pane -t "$D"
