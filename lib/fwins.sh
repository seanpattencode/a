#!/bin/sh
# a fwins — fleet-wide tmux window enumerator for the /fw web view. Emits, one per line:
#   device <TAB> target <TAB> label      (target = local window idx, or ssh:<device>:<idx> for remote)
# STABLE ORDER: local first, then fleet.txt order. Parallel scans land in PER-DEVICE files, never interleaved on
# one stdout — otherwise blocks reorder by whichever ssh answers first and the list visibly jumps every scan.
# A device that fails to answer keeps its LAST-KNOWN rows instead of vanishing (list must not flicker as boxes
# come and go); a device that is THIS box under another name (same hostname, e.g. ubuntuSSD == ubuntuSSD4Tb) is
# dropped as a duplicate. Rate-limited by serve.c (/fwins) — this is an ssh fanout, not a cheap poll.
# Args: $1=DEV $2=DDIR. Run bare to debug.
DEV="${1:-$(hostname)}"; D="${2:-$HOME/a/adata/local}"; OUT="$D/fleetwins.txt"
command -v flock >/dev/null 2>&1 && { exec 9>"$D/.fwins.lock"; flock -n 9 || exit 0; }  # singleton: never stampede the fanout
TD=$(mktemp -d "${TMPDIR:-/tmp}/fwins.XXXXXX") || exit 0
HN=$(hostname)
tmux list-windows -t a -F '#I|#W' 2>/dev/null | awk -F'|' -v d="$DEV" '{print d"\t"$1"\t"$2}' > "$TD/.local"
devs=$(awk 'NR>1 && $2=="ssh"{print $1}' "$D/fleet.txt" 2>/dev/null)
for h in $devs; do   # hostname first: proves the ssh answered (vs "answered but has no tmux") and unmasks self-routes
  ( timeout 10 a ssh "$h" 'hostname; tmux list-windows -t a -F "#I|#W"' </dev/null 2>/dev/null \
      | awk -F'|' -v d="$h" -v hn="$HN" \
          'NR==1{if($0==hn){print "#DUP";exit}print "#OK";next}{print d"\tssh:"d":"$1"\t"$2}' ) > "$TD/$h" &
done
wait
{ cat "$TD/.local"
  for h in $devs; do
    if [ ! -s "$TD/$h" ]; then awk -F'\t' -v h="$h" '$1==h' "$OUT" 2>/dev/null   # unreachable: keep last-known
    elif [ "$(head -1 "$TD/$h")" = "#DUP" ]; then :                              # this box under another name
    else tail -n +2 "$TD/$h"; fi                                                 # drop the #OK marker
  done
} > "$TD/.out"
[ -s "$TD/.out" ] && mv "$TD/.out" "$OUT"
rm -rf "$TD"
