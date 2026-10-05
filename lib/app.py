"""a app [path] — own-window web UI as a real Chrome app window (--app=, own profile).
GTK4+WebKitGTK dropped 2026-10-01, don't go back: GTK4's default Vulkan GSK on NVIDIA/Wayland
missed 26/700 frames (VK_SUBOPTIMAL_KHR on every present, 57ms hitches) and WebKitGTK pins
~60fps on a 144Hz panel; chrome --app on the same scroll rig: 141fps, 1 drop in 1692.
GSK_RENDERER=gl only trims the hitches — the 60fps cap stays. Stable chrome, never canary
(canary ignores sway resizes, window frozen at birth size)."""
import os,sys
A=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
U='http://localhost:1111'+''.join(sys.argv[2:])
if os.fork():print('→ chrome --app '+U,flush=True);os._exit(0)
os.setsid();d=os.open(os.devnull,os.O_RDWR);os.dup2(d,0);os.dup2(d,1);os.dup2(d,2)
os.execvp('google-chrome',['google-chrome','--user-data-dir='+A+'/adata/local/appchrome','--no-first-run','--no-default-browser-check','--ozone-platform=wayland','--app='+U])
