#!/bin/sh
# a fleet-win — LIVE fleet switching. [SEAN, verbatim 2026-08-05]: "i need a live swithcing thing
# almost tmux win the only difference is the switching button is bigger and goes to all devices not one"
# One local tmux session `fleet`: one window per REACHABLE device, each window = live attach into that
# device's a tmux (you land typing). BIG buttons, no prefix, work from ANYWHERE incl. inside a remote:
#   F8 = jump into fleet + NEXT device · F7 = PREV device · F9 = bounce back to where you were
# Rerun to rebuild against current reachability:  sh ~/a/lib/fw.sh
S=fleet; R="$HOME/a/adata/git/ssh"; ME=$(hostname)
tmux kill-session -t $S 2>/dev/null
tmux new-session -d -s $S -n "$ME" "TMUX= tmux attach -t a || sh"
tmux set -t $S automatic-rename off; tmux set -t $S allow-rename off   # window name = DEVICE, never the remote's guess
for d in $(grep -h '^Name:' "$R"/*.txt 2>/dev/null | tr -d '\r' | sed 's/Name: *//;s/[[:space:]]*$//;s/-\(lan\|wan\|usb\|hot\|relay\)$//' | sort -u); do
  [ "$d" = "$ME" ] && continue
  ( timeout 4 a ssh "$d" true >/dev/null 2>&1 &&
    tmux new-window -d -t $S -n "$d" "while :; do a ssh $d; echo '[fw] $d dropped - retry 3s (q=shell)'; sleep 3; done" ) &
done; wait
sleep 2   # a ssh renames each window to its RESOLVED route (feature: route visible); settle, then prune
tmux list-windows -t $S -F '#{window_index} #{window_name}' | awk -v me="$(echo "$ME"|tr A-Z a-z)" \
  'NR>1{n=tolower($2); if(index(n,me)==1 || seen[n]++) print $1}' | sort -rn | \
  while read -r i; do tmux kill-window -t "$S:$i"; done   # drop self-routes + case/route dupes (keep first)
tmux bind -n F8 switch-client -t $S \; next-window -t $S
tmux bind -n F7 switch-client -t $S \; previous-window -t $S
tmux bind -n F9 switch-client -l
grep -q 'fw.sh fleet keys' ~/.tmux.conf 2>/dev/null || printf '\n# fw.sh fleet keys (big switch button)\nbind -n F8 switch-client -t fleet \; next-window -t fleet\nbind -n F7 switch-client -t fleet \; previous-window -t fleet\nbind -n F9 switch-client -l\n' >> ~/.tmux.conf
echo "fleet windows: $(tmux list-windows -t $S -F '#{window_name}' 2>/dev/null | tr '\n' ' ')"
echo "F8 = next device (from anywhere) · F7 = prev · F9 = back"
