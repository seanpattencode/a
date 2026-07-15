# a shot [outdir] — one screenshot (focused output) + one camera frame, timestamped.
# The periodic-stills capture primitive: minutes-scale productivity/prod data (cron/loop this).
# Graceful: grabs whatever exists — headless robot = cam only, camera-less box = screen only.
# Record-at-source: writes to ~/a/adata/local/shots (gitignored, NOT synced); tier a derivative to cloud yourself.
D="${1:-$HOME/a/adata/local/shots}"; mkdir -p "$D" || exit 1
T=$(date +%Y%m%dT%H%M%S); got=""
# --- screenshot: focused sway output (raw grab, jpg via magick; grim -s pixman is the 200ms trap) ---
if command -v grim >/dev/null 2>&1 && [ -n "$WAYLAND_DISPLAY" ]; then
  R="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"; S=$(ls "$R"/sway-ipc.*.sock 2>/dev/null | head -1)
  O=$(SWAYSOCK=$S swaymsg -t get_outputs 2>/dev/null | grep -oE '"name": "[^"]+"' | head -1 | cut -d'"' -f4)
  f="$D/${T}_screen.jpg"
  if grim ${O:+-o $O} - 2>/dev/null | magick png:- -quality 72 "$f" 2>/dev/null && [ -s "$f" ]; then
    got="$got screen[$(du -h "$f" | cut -f1)]"; else rm -f "$f"; fi
fi
# --- camera: first working V4L2 capture node (try 720p, fall back to whatever it offers) ---
for v in /dev/video0 /dev/video1 /dev/video2 /dev/video3; do
  [ -e "$v" ] || continue; f="$D/${T}_cam.jpg"
  if { ffmpeg -loglevel error -f v4l2 -input_format mjpeg -video_size 1280x720 -i "$v" -frames:v 1 -y "$f" 2>/dev/null \
       || ffmpeg -loglevel error -f v4l2 -i "$v" -frames:v 1 -y "$f" 2>/dev/null; } && [ -s "$f" ]; then
    got="$got cam[$(du -h "$f" | cut -f1)]"; break; else rm -f "$f"; fi
done
[ -n "$got" ] && echo "✓ shot $T ·$got · $D" || { echo "✗ shot: nothing captured (no wayland display and no camera)"; exit 1; }
