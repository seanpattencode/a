#!/bin/sh
# adata is meant to be your personal portable data layer for anything you might imagine, your digital
# identity. To not be able to access it would be incredibly disruptive. The only way to guarantee it can
# survive against any software issue is to allow it to be easily copyable and then readable after copying
# to be used, which means it should not use encryption by default and the air gap of a usb thumb drive or
# equivalent physical separation is the true and only physically guaranteed method in order to secure the
# data while preserving universal access across applications including language models. This is the
# rationale behind how adata should work and how the copying command should work.
echo "a port — copy adata (your portable, unencrypted data layer) to USB/folder. use: a port [dest] [all=+gitignored] [cloud=+rclone] · no dest=auto-USB"
U=$(id -un); A=$(cd "$(dirname "$0")/.." && pwd); D= C= G=1
s(){ sudo -n "$@" 2>/dev/null; }
for x in "$@"; do case $x in all)G=;; cloud)C=1;; *)D=$x;; esac; done
[ "$D" ] || for m in /media/"$U"/* /run/media/"$U"/* /Volumes/*; do
  [ -w "$m" ] && [ "${m##*/}" != "Macintosh HD" ] && { D=$m; break; }; done
[ "$D" ] || for b in $(lsblk -rno NAME,RM,TYPE 2>/dev/null|awk '$2==1&&$3=="part"{print$1}'); do
  m=$(lsblk -rno MOUNTPOINT "/dev/$b"); [ "$m" ] && { [ -w "$m" ] && { D=$m; break; }; continue; }
  s mount -o uid=$(id -u) "/dev/$b" /mnt||s mount "/dev/$b" /mnt||continue
  s chown "$U" /mnt; [ -w /mnt ] && { D=/mnt; break; }; s umount /mnt; done
[ "$D" ] || { echo "x no usb found — a port <dest> [all] [cloud]"; exit 1; }
T=$D/adata; echo "→ $T"
cpy(){ mkdir -p "$2" && rsync -a --no-o --no-g ${G:+--filter=':- .gitignore'} "$1/" "$2/" && echo "✓ $1" || { echo "x $1"; E=1; }; }
cpy "$A/adata/git" "$T/git"
for f in "$A"/adata/git/workspace/projects/*.txt; do
  n=$(sed -n 's/^Name: //p' "$f" 2>/dev/null); [ "$n" ] || continue
  d=$HOME/$n; [ -d "$d" ] || d=$(sed -n "s|^Path: ~|$HOME|p;s|^Path: ||p" "$f")
  [ "$d" != "$HOME" ] && [ -d "$d/.git" ] && cpy "$d" "$T/projects/$n"
done
[ "$C" ] && for r in $(rclone listremotes 2>/dev/null); do
  rclone copy "$r" "$T/cloud/${r%:}" -P && echo "✓ $r" || { echo "x $r"; E=1; }; done
sync; echo "$(du -sh "$T" 2>/dev/null|cut -f1) total — readback:"; git -C "$T/git" log --oneline -1
exit ${E:-0}
