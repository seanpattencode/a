#!/bin/bash
D=~/projects/a-sync/tasks
for f in "$D"/*.txt; do
  [ -f "$f" ] || continue
  echo; head -1 "$f"; echo
  read -p "[d]el [n]ext [l]ist: " c
  case $c in
    d) rm "$f";;
    l) for x in "$D"/*.txt; do head -1 "$x"; done; exit;;
    n) ;;
    *) exit;;
  esac
done
