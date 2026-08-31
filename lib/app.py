"""a app [path] — own-window web UI; Ctrl+W/T reach tmux."""
import sys,os
try:import gi
except ImportError:os.execv('/usr/bin/python3',['/usr/bin/python3']+sys.argv)  # a's python lacks gi; short argv0 exec-loops
os.fork()and os._exit(0);os.setsid();os.dup2(os.open(os.devnull,2),2)  # detach: no GDK-info window
gi.require_version('Gtk','4.0');gi.require_version('WebKit','6.0')
from gi.repository import Gtk,WebKit,GLib
open(os.environ['HOME']+'/.local/share/applications/a-app.desktop','w').write('[Desktop Entry]\nType=Application\nName=a app\nExec=a app\nIcon=a\n')
GLib.set_prgname('a-app')
w=Gtk.Window(title='a');v=WebKit.WebView();w.set_child(v);v.load_uri('http://localhost:1111'+''.join(sys.argv[2:]))
w.connect('close-request',lambda*_:os._exit(0));w.present();GLib.MainLoop().run()
