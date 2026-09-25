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
def key(_c,kv,kc,st):  # F11 fullscreen; Ctrl+R / F5 = hard reload (WebKitGTK binds no reload key, so refresh did nothing)
 if kv==65480:(w.unfullscreen if w.is_fullscreen() else w.fullscreen)();return True
 if kv==65474 or(st&4 and kv in(114,82)):v.reload_bypass_cache();return True  # 65474=F5, 114/82=r/R, st&4=Ctrl
 return False
k=Gtk.EventControllerKey();k.connect('key-pressed',key);w.add_controller(k)
w.connect('close-request',lambda*_:os._exit(0));w.present();GLib.MainLoop().run()
