"""a app [path] — own-window web UI; native flash window first, title=cold ms."""
import sys,os,time
A=os.path.dirname(os.path.dirname(os.path.abspath(__file__)));B=A+'/adata/local/flash'
if'A_FLASH'not in os.environ:  # once (file re-runs after the execv below): flash NOW, gi+GTK+WebKit boot behind it
 try:
  if os.stat(B).st_mtime<=os.stat(A+'/lib/flash.c').st_mtime:raise OSError
  os.environ['A_FLASH']=str(os.posix_spawn(B,[B],os.environ))
 except OSError:os.environ['A_FLASH']='';os.posix_spawn('/bin/sh',['sh','-c',"X=/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml;cd ${TMPDIR:-/tmp}&&wayland-scanner client-header $X xs.h&&wayland-scanner private-code $X xs.c&&{ tcc -w -I. -o '%s' '%s' xs.c -lwayland-client||/usr/bin/gcc -B/usr/bin -O2 -w -I. -o '%s' '%s' xs.c -lwayland-client;} 2>/dev/null"%(B,A+'/lib/flash.c',B,A+'/lib/flash.c')],os.environ)  # build once; flash joins next launch
T=int(open('/proc/self/stat').read().split()[21])  # pre-fork; execv-safe
try:import gi
except:os.execv('/usr/bin/python3',['/usr/bin/python3']+sys.argv)  # a's python lacks gi
os.fork()and os._exit(0);os.setsid();os.dup2(os.open(os.devnull,2),2)  # detach
gi.require_version('Gtk','4.0');gi.require_version('WebKit','6.0')
from gi.repository import Gtk,WebKit,GLib
GLib.set_prgname('a-app')
v=WebKit.WebView();v.load_uri('http://localhost:1111'+''.join(sys.argv[2:]))  # load first
w=Gtk.Window(title='a');w.set_child(v)
F=os.environ.get('A_FLASH');F and w.connect('map',lambda*_:os.kill(int(F),15))  # real window mapped -> flash dies
v.connect('load-changed',lambda v,e:e==3 and w.set_title('a %d0ms'%(time.clock_gettime(7)*100-T)))  # 7=BOOTTIME
k=Gtk.EventControllerKey() # F11
k.connect('key-pressed',lambda _,k,*a:k==65480 and not(w.fullscreen,w.unfullscreen)[w.is_fullscreen()]())
w.add_controller(k)
w.connect('close-request',lambda*_:os._exit(0));w.present();GLib.MainLoop().run()
