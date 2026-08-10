#!/bin/sh
# prewarm: EVERY window pre-fit to the LARGEST client (arriving never resizes) — a small fleet frame pans, never shrinks the terminal you work in. hook: tm_ensure_conf
T="tmux -S $1";N=$2 C=$3;[ "$C" ]||exit;sleep .2
s=$($T display -p -t "$N" '#{status}');case $s in on)s=1;;of*|'')s=0;esac
set -- $($T lsc -F '#{client_width} #{client_height}'|awk '$1>x{x=$1}$2>y{y=$2}END{print x,y}');[ "$2" ]||exit;W=$1 H=$(($2-s))
$T resizew -t $C -x $W -y $H
$T lsw -t "$N" -f "#{&&:#{==:#{window_active_clients},0},#{!=:#{window_width}x#{window_height},${W}x$H}}" -F "resizew -t #{window_id} -x $W -y $H"|$T source-file /dev/stdin
