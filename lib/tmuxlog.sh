#!/bin/sh
# a tmuxlog — tmux-server black box (2026-08-08 death storm: 2 untraceable deaths/hour, no oom, no core).
# One line per event in adata/local/tmuxdeaths.log: watchdog start, server up, KILLER SIGHTED (any live
# kill-server/pkill-tmux cmdline, w/ pid so ancestry is recoverable), server DIED w/ free-RAM + journal hits.
# Never polls via tmux clients (client floods are themselves a suspected killer): pid resolved once per server
# generation, then /proc watched. Singleton via flock. Start detached: setsid sh lib/tmuxlog.sh >/dev/null 2>&1 &
D="$HOME/a/adata/local"; L="$D/tmuxdeaths.log"
command -v flock >/dev/null 2>&1 && { exec 9>"$D/.tmuxlog.lock"; flock -n 9 || exit 0; }
say(){ echo "$(date '+%F %T') $*" >>"$L"; }
say "watchdog start pid=$$"
while :; do
  P=$(tmux display -p '#{pid}' 2>/dev/null)
  [ -z "$P" ] && { sleep 3; continue; }
  say "server up pid=$P"
  while [ -d "/proc/$P" ]; do
    K=$(pgrep -af 'tmux kill-server|pkill.*tmux|killall.*tmux' 2>/dev/null | grep -v tmuxlog | head -3)
    [ -n "$K" ] && say "KILLER SIGHTED: $(echo "$K" | tr '\n' ';')"
    sleep 2
  done
  say "server pid=$P DIED avail=$(free -m 2>/dev/null | awk '/^Mem/{print $7}')M journal-kill-hits=$(journalctl --user --since '-90s' --no-pager 2>/dev/null | grep -ciE 'oom|killed')"
done
