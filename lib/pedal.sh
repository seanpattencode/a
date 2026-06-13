#!/bin/sh
# a pedal — install USB-footswitch keyd remap from adata (synced) + reload + show. edit map: e adata/git/settings/keyd/footswitch.conf
d="$(cd "$(dirname "$0")/../adata/git/settings/keyd" && pwd)" || { echo "x no keyd config in adata"; exit 1; }
sudo sh -c "cp '$d'/*.conf /etc/keyd/ && (keyd reload 2>/dev/null || keyd.rvaiya reload)" && echo "✓ keyd reloaded from $d"
lsusb | grep -i footswitch || echo "! footswitch not detected — plug it in"
echo "--- mapping ---"; cat "$d"/*.conf
