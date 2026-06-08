#!/bin/sh
# a extload [ext-dir] — open an unpacked extension in Chrome the easy way. No arg: the Chrome extension at lib/bri-chrome.
# Opens chrome://extensions, opens the folder (selected) in your file manager, copies the path to clipboard. DRY=1 = print only.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# reload: poke the FIFO -> a-server WS -> bri-chrome service worker reloads the extension + focused tab (zero-click, like `a bri deploy` for Firefox).
[ "$1" = reload ] && { [ -p /tmp/a_extreload.fifo ] && timeout 0.4 sh -c 'printf x>/tmp/a_extreload.fifo' 2>/dev/null && echo "↻ chrome reloaded" || echo "x no chrome connected — load it via 'a extload' + focus a browser tab to wake the worker"; exit 0; }
D=$(realpath "${1:-$ROOT/lib/bri-chrome}") || exit 1
[ -f "$D/manifest.json" ] || { echo "x no manifest.json in $D"; exit 1; }
C=$(command -v google-chrome-canary||command -v google-chrome-unstable||command -v google-chrome-stable||command -v chromium)||{ echo "x no chrome found"; exit 1; }
printf %s "$D" | { wl-copy 2>/dev/null || xclip -sel clip 2>/dev/null; } || true
if [ -n "$DRY" ]; then echo "[dry] would open $C chrome://extensions/ + fm @ $D";
else "$C" chrome://extensions/ >/dev/null 2>&1 &
  if command -v nautilus >/dev/null; then nautilus --select "$D" >/dev/null 2>&1 &
  else xdg-open "$(dirname "$D")" >/dev/null 2>&1 & fi; fi
cat <<EOF
📦 $(basename "$D") → ${C##*/}   (path copied to clipboard)
If the Extensions page didn't open, type  chrome://extensions  in the address bar.
To load:
  1) toggle Developer mode ON (top-right)
  2) DRAG the "$(basename "$D")" folder onto the page
     — or — Load unpacked → Ctrl+L → paste path → Enter
  3) BRIDGE (bri-chrome automation): click Details → enable "Allow user scripts"
     (Chrome 138+, separate from Developer mode; eval/bridge stay off without it)
  4) open/reload a normal http(s) tab so the userScript injects, then: a extload reload
EOF
