"""a droid [port] — NDK-build a + push + run a serve on android. bypasses termux."""
import os,sys,subprocess as S,time
D=os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
P=sys.argv[2] if len(sys.argv)>2 else "1112"
N=os.environ.get("ANDROID_HOME",os.path.expanduser("~/Android/Sdk"))+"/ndk"
C=f"{N}/{sorted(os.listdir(N))[-1]}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang" if os.path.isdir(N) else ""
d=os.environ.get("ANDROID_SERIAL")or next((l.split()[0] for l in S.run(["adb","devices"],capture_output=True,text=True).stdout.splitlines() if "\tdevice" in l),None)
os.access(C,os.X_OK) and d or sys.exit(f"x need NDK({C}) + adb device")
a=lambda*x:S.check_call(["adb","-s",d,*x],stdout=S.DEVNULL)
S.check_call([C,'-DSRC="/data/local/tmp"',"-w","-O2","-o","/tmp/a-droid",f"{D}/a.c"])
a("push","/tmp/a-droid","/data/local/tmp/a");a("push",f"{D}/lib/ui","/data/local/tmp/lib/ui");a("push",f"{D}/lib/ui_full.html","/data/local/tmp/lib/ui_full.html")
a("shell",f"chmod +x /data/local/tmp/a;pkill -9 -x a 2>/dev/null;sleep 1;setsid -d /data/local/tmp/a serve {P} >/data/local/tmp/srv.log 2>&1 </dev/null &")
a("forward",f"tcp:{P}",f"tcp:{P}");time.sleep(2)
r=S.run(["curl","-sm3","-o/dev/null","-w%{http_code}",f"localhost:{P}/term"],capture_output=True,text=True).stdout
print(f"{'✓' if r=='200' else 'x '+r} http://localhost:{P}/term")
