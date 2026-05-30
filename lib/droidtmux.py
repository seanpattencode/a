"""a droidtmux — build+deploy aarch64 tmux (+libevent, ncurses, tic, terminfo) to /data/local/tmp."""
import os, sys, subprocess as S, urllib.request as U, tarfile, io
H = os.path.expanduser("~")
S0=os.environ.get('ANDROID_HOME') or next((p for p in[H+"/Library/Android/sdk",H+"/Android/Sdk"] if os.path.isdir(p)),H+"/Android/Sdk")
N = f"{S0}/ndk"
v = sorted(os.listdir(N))[-1] if os.path.isdir(N) else sys.exit("x no NDK")
T = f"{N}/{v}/toolchains/llvm/prebuilt";T=f"{T}/{os.listdir(T)[0]}"
J="$(getconf _NPROCESSORS_ONLN)"
ABI=os.environ.get("DABI","arm64-v8a")
CT,HOST={"arm64-v8a":("aarch64-linux-android","aarch64-linux-android"),"armeabi-v7a":("armv7a-linux-androideabi","arm-linux-androideabi")}[ABI]
E = {**os.environ, "PATH": f"{T}/bin:{os.environ['PATH']}", "CC": f"{T}/bin/{CT}29-clang", "AR": f"{T}/bin/llvm-ar", "RANLIB": f"{T}/bin/llvm-ranlib", "LDFLAGS": "-Wl,-z,max-page-size=16384"}
sfx="" if ABI=="arm64-v8a" else "-"+ABI
SRC, PRE = "/tmp/dsrc"+sfx, "/tmp/dstatic"+sfx
os.makedirs(SRC, exist_ok=True)
def dl(u, d):
    if os.path.isdir(d): return
    with U.urlopen(u) as r: tarfile.open(fileobj=io.BytesIO(r.read())).extractall(SRC)
def sh(cmd, cwd, **kw): S.check_call(cmd, cwd=cwd, env=E, shell=isinstance(cmd,str), **kw)
if not os.path.exists(PRE+"/lib/libevent.a"):
    dl("https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz", f"{SRC}/libevent-2.1.12-stable")
    sh(f"./configure --host={HOST} --prefix={PRE} --disable-shared --enable-static --disable-openssl --disable-samples --disable-libevent-regress --disable-debug-mode >/dev/null && make -j{J} >/dev/null && make install >/dev/null", f"{SRC}/libevent-2.1.12-stable")
NC = f"{SRC}/ncurses-6.4"
if not os.path.exists(f"{NC}/progs/tic"):
    dl("https://invisible-mirror.net/archives/ncurses/ncurses-6.4.tar.gz", NC)
    fp=f"{NC}/ncurses/tinfo/write_entry.c";s=open(fp).read();open(fp,"w").write(s.replace('_nc_syserr_abort("can\'t link %s to %s", filename, linkname);','_nc_warning("link failed, writing copy %s", linkname); write_file(linkname, tp);'))
    sh(f"./configure --host={HOST} --prefix={PRE} --without-shared --without-ada --without-tests --without-manpages --without-debug --without-cxx-binding --with-default-terminfo-dir=/data/local/tmp/terminfo --disable-stripping >/dev/null && make -j{J} >/dev/null && (make install >/dev/null 2>&1 || true)", NC)
    os.path.exists(f"{PRE}/lib/libtinfo.a") or os.symlink("libncurses.a", f"{PRE}/lib/libtinfo.a")
TB = f"{SRC}/tmux-build-a"
if not os.path.exists(TB):
    if os.path.isdir(f"{H}/tmux-build"):S.check_call(f"cp -r {H}/tmux-build {TB} && find {TB} -name '*.o' -delete && rm -f {TB}/tmux && cd {TB} && touch -r Makefile.in aclocal.m4 configure configure.ac Makefile.am 2>/dev/null || true", shell=True)
    else:dl("https://github.com/tmux/tmux/releases/download/3.5a/tmux-3.5a.tar.gz",f"{SRC}/tmux-3.5a");os.rename(f"{SRC}/tmux-3.5a",TB)
    open(f"{TB}/compat/forkpty-linux.c","w").write("/* stub */\n")
    import re
    fp=f"{TB}/compat.h";s=open(fp).read();open(fp,"w").write(re.sub(r"^pid_t\s+forkpty\(int \*, char \*, struct termios \*, struct winsize \*\);","/* patched */",s,flags=re.M))
    for f, is_s in [('server.c', True), ('client.c', False)]:
        src = open(f"{TB}/{f}").read()
        old_block = 'size = strlcpy(sa.sun_path, ' + ('socket_path' if is_s else 'path') + ', sizeof sa.sun_path);\n\tif (size >= sizeof sa.sun_path) {\n\t\terrno = ENAMETOOLONG;\n\t\t' + ('goto fail' if is_s else 'return (-1)') + ';\n\t}' + ('\n\tunlink(sa.sun_path);' if is_s else '')
        new_block = '{sa.sun_path[0]=0; size=strlcpy(sa.sun_path+1,' + ('socket_path' if is_s else 'path') + ',sizeof(sa.sun_path)-1)+1;}\n\tif (size >= sizeof sa.sun_path) { errno=ENAMETOOLONG; ' + ('goto fail' if is_s else 'return -1') + '; }'
        src = src.replace(old_block, new_block)
        src = src.replace(f'if ({"bind" if is_s else "connect"}(fd, (struct sockaddr *)&sa, sizeof sa) == -1) {{', f'if ({"bind" if is_s else "connect"}(fd, (struct sockaddr *)&sa, offsetof(struct sockaddr_un,sun_path)+size) == -1) {{')
        if 'offsetof' in src and '#include <stddef.h>' not in src: src = '#include <stddef.h>\n' + src
        open(f"{TB}/{f}", 'w').write(src)
if not os.path.exists(f"{TB}/tmux"):
    e2 = {**E, "CFLAGS":f"-I{PRE}/include -I{PRE}/include/ncurses -Os -fPIE", "LDFLAGS":f"-L{PRE}/lib -pie {E['LDFLAGS']}", "LIBEVENT_CFLAGS":f"-I{PRE}/include", "LIBEVENT_LIBS":f"-L{PRE}/lib -levent", "LIBNCURSES_CFLAGS":f"-I{PRE}/include/ncurses", "LIBNCURSES_LIBS":f"-L{PRE}/lib -lncurses", "ac_cv_func_forkpty":"yes"}
    S.check_call(f"./configure --host={HOST} --disable-utf8proc --disable-dependency-tracking >/dev/null && make -j{J}", cwd=TB, env=e2, shell=True)
for b in [f"{TB}/tmux", f"{NC}/progs/tic"]: S.check_call([f"{T}/bin/llvm-strip", b])
if os.environ.get("DPUSH","1")=="0":print(f"✓ built {ABI} tmux+tic");sys.exit(0)
d = next((l.split()[0] for l in S.run(["adb","devices"],capture_output=True,text=True).stdout.splitlines() if "\tdevice" in l),None) or sys.exit("x no adb device")
for src, dst in [(f"{TB}/tmux","/data/local/tmp/tmux"), (f"{NC}/progs/tic","/data/local/tmp/tic"), (f"{NC}/misc/terminfo.src","/data/local/tmp/terminfo.src")]:
    S.check_call(["adb","-s",d,"push",src,dst], stdout=S.DEVNULL)
S.check_call(["adb","-s",d,"shell","chmod +x /data/local/tmp/tmux /data/local/tmp/tic; rm -rf /data/local/tmp/terminfo; mkdir /data/local/tmp/terminfo; /data/local/tmp/tic -o /data/local/tmp/terminfo /data/local/tmp/terminfo.src 2>/dev/null"])
print(f"✓ tmux+terminfo on {d}")
