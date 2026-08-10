#!/bin/sh
# prewarm: unviewed windows pre-fit to this client (arriving never resizes); in-view fits its smallest VIEWER — wider than a viewer pans. hook: tm_ensure_conf
T="tmux -S $1";N=$2 C=$3;[ "$C" ]||exit;sleep .2
set -- $($T display -p -t "$N" '#{client_width} #{client_height} #{status}');[ "$2" ]||exit
s=$3;case $s in on)s=1;;of*|'')s=0;esac;W=$1 H=$(($2-s))
set -- $($T lsc -f "#{==:#{window_id},$C}" -F '#{client_width} #{client_height}'|awk '$1<x||!x{x=$1}$2<y||!y{y=$2}END{print x,y}')
[ "$2" ]&&$T resizew -t $C -x $1 -y $(($2-s))
$T lsw -t "$N" -f "#{&&:#{==:#{window_active_clients},0},#{!=:#{window_width}x#{window_height},${W}x$H}}" -F "resizew -t #{window_id} -x $W -y $H"|$T source-file /dev/stdin
