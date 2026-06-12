#!/bin/sh
# adata is meant to be your personal portable data layer for anything you might imagine, your digital
# identity. To not be able to access it would be incredibly disruptive. The only way to guarantee it can
# survive against any software issue is to allow it to be easily copyable and then readable after copying
# to be used, which means it should not use encryption by default and the air gap of a usb thumb drive or
# equivalent physical separation is the true and only physically guaranteed method in order to secure the
# data while preserving universal access across applications including language models. This is the
# rationale behind how adata should work and how the copying command should work.
# a port [dest] [all] [cloud] — copy adata/git + project repos to dest/adata (no dest: first usb drive).
# default skips gitignored files; all includes them; cloud also pulls every rclone remote.
A=$(cd "$(dirname "$0")/.." && pwd); D= C= G=1
for x in "$@"; do case $x in all) G=;; cloud) C=1;; *) D=$x;; esac; done
[ -z "$D" ] && for m in /media/"$(id -un)"/* /run/media/"$(id -un)"/* /Volumes/*; do
    [ -w "$m" ] && [ "${m##*/}" != "Macintosh HD" ] && D=$m && break; done
[ -z "$D" ] && for b in $(lsblk -rno NAME,RM,TYPE 2>/dev/null|awk '$2==1&&$3=="part"{print $1}'); do
    mp=$(lsblk -rno MOUNTPOINT "/dev/$b"); [ -n "$mp" ] && [ -w "$mp" ] && { D=$mp; break; }
    [ -n "$mp" ] && continue
    sudo -n mount -o uid=$(id -u) "/dev/$b" /mnt 2>/dev/null||sudo -n mount "/dev/$b" /mnt 2>/dev/null||continue
    sudo -n chown "$(id -un)" /mnt 2>/dev/null; [ -w /mnt ] && { D=/mnt; break; }; sudo -n umount /mnt 2>/dev/null; done
[ -z "$D" ] && { echo "x no usb found — a port <dest> [all] [cloud]"; exit 1; }
T=$D/adata; echo "→ $T"
cpy(){ mkdir -p "$2" && rsync -a ${G:+--filter=':- .gitignore'} "$1/" "$2/" && echo "✓ $1" || { echo "x $1"; E=1; }; }
cpy "$A/adata/git" "$T/git"
for f in "$A"/adata/git/workspace/projects/*.txt; do
    n=$(sed -n 's/^Name: //p' "$f" 2>/dev/null); [ -z "$n" ] && continue; d=$HOME/$n
    [ -d "$d" ] || d=$(sed -n "s|^Path: ~|$HOME|p;s|^Path: ||p" "$f")
    [ "$d" != "$HOME" ] && [ -d "$d/.git" ] && cpy "$d" "$T/projects/$n"
done
[ -n "$C" ] && for r in $(rclone listremotes 2>/dev/null); do
    rclone copy "$r" "$T/cloud/${r%:}" -P && echo "✓ $r" || { echo "x $r"; E=1; }
done
sync; echo "$(du -sh "$T" 2>/dev/null|cut -f1) total — readback:"; git -C "$T/git" log --oneline -1
exit ${E:-0}
