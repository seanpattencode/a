#!/bin/sh
# a usb [info|get|test|write [dev]]
U=https://dl.getaurora.dev/aurora-stable-webui-x86_64.iso
I=$HOME/Downloads/${U##*/}
O=/usr/share/OVMF/OVMF
sp(){ stty -echo 2>/dev/null;read -p "sudo: " P;stty echo 2>/dev/null;echo; }
S(){ echo "$P"|sudo -S "$@"; }
T=${2:-/dev/sdf}
case ${1:-info} in
write)sp
  [ "$(cat /sys/block/${T##*/}/removable 2>/dev/null)" = 1 ]||{ echo "x $T not removable";exit 1; }
  S umount $T* 2>/dev/null
  S dd if=$I of=$T bs=4M status=progress conv=fsync
  S partprobe $T;lsblk $T;;
test)X=${2:-$I};SRC="-cdrom $X -boot d";PRE=
  case $X in /dev/*)sp;SRC="-drive file=$X,format=raw,if=virtio";PRE="sudo -S";;esac
  echo "$P"|$PRE qemu-system-x86_64 -enable-kvm -m 4096 -smp 4 \
    -drive if=pflash,format=raw,readonly=on,file=${O}_CODE_4M.fd \
    -drive if=pflash,format=raw,file=${O}_VARS_4M.fd,snapshot=on $SRC;;
get)mkdir -p ${I%/*}
  Z=$(curl -sLI $U|awk 'tolower($1)~/content-length/{print $2+0}')
  [ "$(stat -c%s $I 2>/dev/null)" = "$Z" ]||curl -L --fail -C - --retry 10 -o $I $U||exit 1
  H=$(curl -fsSL $U-CHECKSUM);echo "${H%% *}  $I"|sha256sum -c -;;
*)sp;D=${2:-/dev/sdf1};M=/mnt/usb
  S mkdir -p $M;findmnt $D >/dev/null||S mount $D $M
  findmnt $D;df -h $D;lsblk -f $D;;
esac
