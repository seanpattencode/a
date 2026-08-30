#!/bin/sh
# a actmig — TEMPORARY: fold old per-command activity files into local/activity/YYYY-MM_DEV.txt (sorted), rm originals. Once per box after update.
L=$(cd "$(dirname "$0")/.." && pwd)/adata; A=$L/local/activity; mkdir -p "$A"
for d in "$A" "$L/git/activity"; do [ -d "$d" ] || continue
  find "$d" -maxdepth 2 -name '2???????T*_*.txt' 2>/dev/null | sed 's|.*/||;s/^\(....\)\(..\)[^_]*_\(.*\)\.txt$/\1-\2 \3/' | sort -u |
  while read -r ym dev; do p="$(echo "$ym"|tr -d -)??T*_${dev}.txt"
    find "$d" -maxdepth 2 -name "$p" -exec cat {} + >> "$A/${ym}_${dev}.txt" 2>/dev/null
    find "$d" -maxdepth 2 -name "$p" -delete 2>/dev/null
  done; rmdir "$d"/2*/ 2>/dev/null; done
for f in "$A"/2*_*.txt; do [ -f "$f" ] && sort -o "$f" "$f"; done
echo "done: $(ls "$A" | wc -l) files in $A"
