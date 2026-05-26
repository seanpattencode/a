#!/bin/sh
G=/sys/kernel/config/usb_gadget/a
S(){ echo "$1"|sudo tee "$2" >/dev/null; }
case "$1" in
on)
  sudo modprobe libcomposite
  sudo mkdir -p $G/strings/0x409 $G/configs/c.1/strings/0x409 $G/functions/hid.kbd $G/functions/hid.mouse $G/functions/ecm.usb0
  cd $G || exit 1
  S 0x1d6b idVendor; S 0x0104 idProduct
  S a-gadget strings/0x409/product; S 0001 strings/0x409/serialnumber; S sean strings/0x409/manufacturer
  S "kbd+mouse+eth" configs/c.1/strings/0x409/configuration
  S 1 functions/hid.kbd/protocol; S 1 functions/hid.kbd/subclass; S 8 functions/hid.kbd/report_length
  printf '\5\1\11\6\241\1\5\7\31\340\51\347\25\0\45\1\165\1\225\10\201\2\225\1\165\10\201\3\225\5\165\1\5\10\31\1\51\5\221\2\225\1\165\3\221\3\225\6\165\10\25\0\45\145\5\7\31\0\51\145\201\0\300'|sudo tee functions/hid.kbd/report_desc >/dev/null
  S 0 functions/hid.mouse/protocol; S 0 functions/hid.mouse/subclass; S 4 functions/hid.mouse/report_length
  printf '\5\1\11\2\241\1\11\1\241\0\5\11\31\1\51\3\25\0\45\1\225\3\165\1\201\2\225\1\165\5\201\3\5\1\11\60\11\61\11\70\25\201\45\177\165\10\225\3\201\6\300\300'|sudo tee functions/hid.mouse/report_desc >/dev/null
  S 02:1a:11:00:00:01 functions/ecm.usb0/host_addr; S 02:1a:11:00:00:02 functions/ecm.usb0/dev_addr
  for f in hid.kbd hid.mouse ecm.usb0; do sudo ln -sf $G/functions/$f configs/c.1/; done
  ls /sys/class/udc|sudo tee UDC >/dev/null
  sleep 1; sudo ip addr add 192.168.7.1/24 dev usb0 2>/dev/null; sudo ip link set usb0 up 2>/dev/null
  # configfs is RAM-only — gadget config is wiped on reboot. Install a one-shot
  # systemd unit so the gadget comes back automatically and USB-C ssh works
  # before network is up. Idempotent: only writes the unit once.
  U=/etc/systemd/system/a-gadget.service
  [ -f $U ]||{ printf "[Unit]\nAfter=network-pre.target\n[Service]\nType=oneshot\nExecStart=%s on\nRemainAfterExit=yes\n[Install]\nWantedBy=multi-user.target\n" "$(realpath "$0")"|sudo tee $U >/dev/null; sudo systemctl enable a-gadget 2>/dev/null; echo "+ systemd unit"; }
  echo "✓ kbd=/dev/hidg0 mouse=/dev/hidg1 eth=192.168.7.1 (windows static 192.168.7.2)"
  ;;
off)
  [ -d $G ]&&{ echo ""|sudo tee $G/UDC >/dev/null 2>&1; for l in $G/configs/c.1/hid.kbd $G/configs/c.1/hid.mouse $G/configs/c.1/ecm.usb0; do sudo rm -f $l; done; for d in $G/functions/hid.kbd $G/functions/hid.mouse $G/functions/ecm.usb0 $G/configs/c.1/strings/0x409 $G/configs/c.1 $G/strings/0x409; do sudo rmdir $d 2>/dev/null; done; sudo rmdir $G 2>/dev/null; }
  echo "✓ off"
  ;;
*) echo "a gadget on|off" ;;
esac
