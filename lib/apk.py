"""a apk [path] [serial] [noauth]   |   a apk auth [serial]

WHY THE APK EXISTS OVER TERMUX (keep this division — termux owns ALL logic + HTML + as much GUI as possible, to cut duplication + tokens):
The apk is the thin shell for the only three things a termux terminal script can't do itself on Android OS:
  1. visualization layer (the GUI).
  2. termux keeps dying on Android and needs restarting — the apk, being durable, keeps/restarts it alive.
  3. the numerous OS-level things only an installed apk can reach that a termux script cannot (OS + visualization).
Everything else shells out to the termux `a` install — the apk is a UI caller, NOT a second runtime.

After install, `a apk` auto-provisions the phone's termux with this dev box's gh + rclone
creds (~/.config/gh/hosts.yml, ~/.config/rclone/rclone.conf) and points adata/git's origin
at seanpattencode/a-git — so the web-UI's note sync (gh contents API) + rclone work on the
phone. The apk does this via termux RUN_COMMAND (adb can't reach termux's home); idempotent
and non-destructive (sets the remote, never clones/wipes). `noauth` skips it; `a apk auth
[serial]` re-pushes creds + sets the remote without rebuilding.

Inspect / debug the APK's live web UI from your dev machine (the app runs `a serve` on device port 1112):
    adb -s <serial> forward tcp:9112 tcp:1112      # one-time; local 9112 -> device 1112
    curl -s localhost:9112/                         # the index HTML the app actually serves
    curl -s localhost:9112/term                     # terminal page
    curl -s localhost:9112/api/omni --data-urlencode 'q=ssh <dev> a c <prompt>'   # drive it (e.g. ssh fedora-wan + launch claude)
Chrome DevTools also works: chrome://inspect (WebContentsDebugging is on) to see the in-app WebView.
Drive the UI programmatically (no coordinate-tapping) — pick any menu item / open the menu:
    adb -s <serial> shell am start -n com.aios.a/.M --es nav note      # items: Native | web boxes (home proj op dash stream job note task term book docs cloud prompt) | Setup Read Rec Termux homebox Bubble
    adb -s <serial> shell am start -n com.aios.a/.M --ez menu true     # open the menu overlay
ui_full.html is re-copied from assets on every launch, so a fresh `a apk` reinstall always updates the UI.
"""
import os,subprocess as S,shutil,glob,sys
P="com.aios.a"
KT=r'''@file:Suppress("DEPRECATION","OVERRIDE_DEPRECATION")
package com.aios.a
import android.app.Activity;import android.content.*;import android.os.*;import android.webkit.*;import android.view.*;import android.graphics.*;import android.widget.*
import java.io.File;import java.io.OutputStream;import java.net.Socket
private const val BASE="http://127.0.0.1:1112"
// "next tmux session" that works locally AND inside an `a ssh` session. Targets the ATTACHED client's session
// (not the ambiguous implicit "current", which the grouped per-client sessions mis-resolve from a detached RUN_COMMAND).
// `a ssh` runs via sshpass, so the pane's foreground command is ssh/mosh/sshpass — inject prefix+n as keystrokes so they ride
// the ssh pipe to the REMOTE tmux (verified switching ubuntu's window); otherwise switch the local tmux.
const val TMUXNEXT="S=\$(tmux list-clients -F '#{client_session}'|head -1);[ -n \"\$S\" ]||S=\$(tmux display -p '#{session_name}');C=\$(tmux display -t \"\$S\" -p '#{pane_current_command}');case \"\$C\" in ssh|mosh|sshpass)tmux send-keys -t \"\$S\" C-b n;;*)tmux next-window -t \"\$S\";;esac"
// All a-logic shells out to the termux `a` install (the device's real terminal); the APK is a UI caller. RUN_COMMAND also cold-starts termux if it crashed. arg passed as $1 (no injection).
fun txRun(c:Context,script:String,vararg arg:String){val i=Intent().setClassName("com.termux","com.termux.app.RunCommandService").setAction("com.termux.RUN_COMMAND");i.putExtra("com.termux.RUN_COMMAND_PATH","/data/data/com.termux/files/usr/bin/bash");i.putExtra("com.termux.RUN_COMMAND_ARGUMENTS",arrayOf("-lc",script,"a",*arg));i.putExtra("com.termux.RUN_COMMAND_BACKGROUND",true);try{if(Build.VERSION.SDK_INT>=26)c.startForegroundService(i) else c.startService(i)}catch(e:Exception){}}
class M:Activity(){
companion object{init{System.loadLibrary("anative")};@JvmStatic var su=false}
private lateinit var w:WebView;private val h=Handler(Looper.getMainLooper());private var n=0;private var cur="$BASE/";private var loaded=false;private var onSpa=true
private var wsOut:OutputStream?=null;private var rt:LinearLayout?=null
private fun bubOn()=(getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).getRunningServices(99).any{it.service.className=="com.aios.a.BubbleService"}
private val wsExec=java.util.concurrent.Executors.newSingleThreadExecutor()
private fun pg(s:String)=w.loadDataWithBaseURL(null,"<body style='font:18px monospace;padding:20px;background:#000;color:#0f0'>$s","text/html","utf-8",null)
private fun jsEval(s:String)=h.post{w.evaluateJavascript(s,null)}
@JavascriptInterface fun retry(){h.post{spawn();n=0;w.loadUrl(cur)}}
@JavascriptInterface fun wsOpen(url:String){wsExec.submit{try{val s=Socket("127.0.0.1",1112);wsOut=s.getOutputStream()
val k=android.util.Base64.encodeToString(ByteArray(16).also{java.security.SecureRandom().nextBytes(it)},android.util.Base64.NO_WRAP)
wsOut!!.write("GET /ws HTTP/1.1\r\nHost: 127.0.0.1:1112\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: $k\r\nSec-WebSocket-Version: 13\r\n\r\n".toByteArray())
val ins=s.getInputStream();val hd=StringBuilder();while(!hd.endsWith("\r\n\r\n")){val b=ins.read();if(b<0)return@submit;hd.append(b.toChar())}
jsEval("window._wsOpen&&window._wsOpen()")
val buf=ByteArray(65536);while(true){val op=ins.read();if(op<0)break;val lb=ins.read();if(lb<0)break;var len=lb and 0x7f
if(len==126){len=(ins.read() shl 8) or ins.read()}else if(len==127){len=0;for(i in 0..7)len=(len shl 8) or ins.read()}
var got=0;while(got<len){val n=ins.read(buf,got,len-got);if(n<=0)return@submit;got+=n}
if((op and 0xf)==8)break
jsEval("window._wsMsg&&window._wsMsg(atob('${android.util.Base64.encodeToString(buf,0,len,android.util.Base64.NO_WRAP)}'))")}
jsEval("window._wsClose&&window._wsClose(1000)")
}catch(e:Exception){jsEval("window._wsClose&&window._wsClose(1011)")}}}
@JavascriptInterface fun wsSend(data:String){wsExec.submit{try{val bs=data.toByteArray(Charsets.UTF_8);val o=java.io.ByteArrayOutputStream();o.write(0x81)
when{bs.size<126->o.write(0x80 or bs.size);bs.size<65536->{o.write(0x80 or 126);o.write(bs.size shr 8);o.write(bs.size and 0xff)}}
o.write(ByteArray(4));o.write(bs)
wsOut?.write(o.toByteArray());wsOut?.flush()}catch(e:Exception){}}}
private val SHIM="(function(){var _w=null;window.WebSocket=function(url){_w=this;this.readyState=0;this.send=function(d){A.wsSend(d+'')};this.close=function(){this.readyState=3};A.wsOpen(url)};window._wsOpen=function(){if(_w){_w.readyState=1;if(_w.onopen)_w.onopen()}};window._wsMsg=function(d){if(_w&&_w.onmessage)_w.onmessage({data:d})};window._wsClose=function(c){if(_w){_w.readyState=3;if(_w.onclose)_w.onclose({code:c,wasClean:false})}}})()"
private var openMenu:(()->Unit)?=null;private var nav:((String)->Unit)?=null
/* programmatic menu control: am start -n com.aios.a/.M --es nav note   (or --ez menu true to open the menu) */
private fun applyNav(i:Intent?):Boolean{val n=i?.getStringExtra("nav");if(n!=null)nav?.invoke(n);if(i?.getBooleanExtra("menu",false)==true)openMenu?.invoke()
// `a apk` (desktop) adb-pushes the dev box's gh hosts.yml + rclone.conf to /data/local/tmp (file sync, so contents never hit logcat — unlike intent extras, which adbd logs verbatim) then fires --ez prov. Only the apk holds termux's RUN_COMMAND grant, so it copies them into termux ~/.config (chmod 600), runs gh auth setup-git, and ensures adata/git's origin points at a-git. note sync uses the gh contents API (note_url's PUT — see lib/git.c), which only needs the origin URL to derive owner/repo, NOT a full clone — so we just set the remote (no rm, no clone, no data loss). http.version=HTTP/1.1 is set because the phone's git stalls on HTTP/2 smart-POST (clone-fetch/push hang while GET/ls-remote work). a apk then wipes the staging copies.
if(i?.getBooleanExtra("prov",false)==true)txRun(this,"mkdir -p ~/.config/gh ~/.config/rclone;[ -f /data/local/tmp/a_gh ]&&{ cp /data/local/tmp/a_gh ~/.config/gh/hosts.yml;chmod 600 ~/.config/gh/hosts.yml;};[ -f /data/local/tmp/a_rc ]&&{ cp /data/local/tmp/a_rc ~/.config/rclone/rclone.conf;chmod 600 ~/.config/rclone/rclone.conf;};command -v gh>/dev/null||exit 0;gh auth setup-git 2>/dev/null;git config --global http.version HTTP/1.1;D=~/a/adata/git;git -C \"\$D\" remote get-url origin 2>/dev/null|grep -q a-git||{ git -C \"\$D\" remote add origin https://github.com/seanpattencode/a-git.git 2>/dev/null;git -C \"\$D\" remote set-url origin https://github.com/seanpattencode/a-git.git 2>/dev/null;}")
return n!=null||i?.getBooleanExtra("prov",false)==true}
override fun onNewIntent(i:Intent){super.onNewIntent(i);setIntent(i);applyNav(i)}
override fun onBackPressed(){val o=openMenu;if(w.visibility==View.VISIBLE&&w.canGoBack())w.goBack() else if(o!=null)o() else super.onBackPressed()}
override fun onResume(){val _wt=android.os.SystemClock.elapsedRealtime();super.onResume();if(!su){su=true;setup()};spawn();startWd(this);if(!loaded){n=0;h.postDelayed({if(!loaded)w.loadUrl(cur)},700)};rt?.setPadding(0,0,0,if(bubOn())(resources.displayMetrics.density*52).toInt() else 0);rt?.post{android.util.Log.i("aPerf","warm: resume->frame "+(android.os.SystemClock.elapsedRealtime()-_wt)+"ms")}}
private val nl by lazy{applicationInfo.nativeLibraryDir}
private fun setup(){val ui=File(filesDir,"lib");ui.mkdirs();val up=File(ui,"ui_full.html");assets.open("ui_full.html").use{i->up.outputStream().use{o->i.copyTo(o)}}  /* always refresh: reinstall must update the UI */
val ti=File(filesDir,"terminfo");if(!File(ti,"x/xterm-256color").exists()){ti.deleteRecursively();ti.mkdirs();val src=File(filesDir,"terminfo.src");if(!src.exists())assets.open("terminfo.src").use{i->src.outputStream().use{o->i.copyTo(o)}}
ProcessBuilder("$nl/libtic.so","-o",ti.absolutePath,src.absolutePath).redirectErrorStream(true).redirectOutput(File(filesDir,"tic.log")).start().waitFor()}
val bin=File(filesDir,"bin");bin.mkdirs();for(p in listOf("a" to "liba.so","tmux" to "libtmux.so","tic" to "libtic.so","dbclient" to "libssh.so","ssh" to "libsshwrap.so","sshpass" to "libsshwrap.so","rclone" to "librclone.so")){val l=File(bin,p.first);try{android.system.Os.remove(l.absolutePath)}catch(e:Exception){};try{android.system.Os.symlink("$nl/${p.second}",l.absolutePath)}catch(e:Exception){}}
for(sub in listOf("ssh","workspace/projects","workspace/cmds")){val sd=File(filesDir,"adata/git/$sub");sd.mkdirs();try{for(n in assets.list("git/$sub")?:emptyArray()){assets.open("git/$sub/$n").use{i->File(sd,n).outputStream().use{o->i.copyTo(o)}}}}catch(e:Exception){}}}
private fun spawn(){txRun(this,"a serve 1112")}  // termux serves on shared loopback :1112; WebView connects to it. onResume re-ensures serve but never reloads the page, so app-switch preserves your current box.
private fun atlas(sz:Float):IntArray{val p=Paint().apply{textSize=sz;color=-1;isAntiAlias=true;typeface=Typeface.MONOSPACE};val cw=p.measureText("M").toInt()+1;val ch=(-p.ascent()+p.descent()).toInt()+1;val b=Bitmap.createBitmap(cw*95,ch,Bitmap.Config.ARGB_8888);Canvas(b).let{c->for(i in 0 until 95)c.drawText(((32+i).toChar()).toString(),(i*cw).toFloat(),-p.ascent(),p)};val r=IntArray(2+cw*95*ch);r[0]=cw;r[1]=ch;b.getPixels(r,2,cw*95,0,0,cw*95,ch);b.recycle();return r}
@android.annotation.SuppressLint("ClickableViewAccessibility")
override fun onCreate(b:Bundle?){super.onCreate(b)
WebView.setWebContentsDebuggingEnabled(true)
w=WebView(this).apply{settings.javaScriptEnabled=true;addJavascriptInterface(this@M,"A");setBackgroundColor(Color.BLACK)
webChromeClient=object:WebChromeClient(){override fun onConsoleMessage(m:ConsoleMessage):Boolean{android.util.Log.w("AWV","[${m.messageLevel()}] ${m.message()} @ ${m.sourceId()}:${m.lineNumber()}");return true}}
webViewClient=object:WebViewClient(){override fun onPageFinished(v:WebView,url:String){v.evaluateJavascript(SHIM,null);if(url.startsWith("http")){loaded=true;android.util.Log.i("aPerf","cold: home painted "+(android.os.SystemClock.elapsedRealtime()-android.os.Process.getStartElapsedRealtime())+"ms after proc start")}}
override fun onReceivedError(v:WebView,r:WebResourceRequest,e:WebResourceError){if(r.isForMainFrame){if(n++<8){pg("<h2>Starting a serve...</h2>$n/8");h.postDelayed({v.loadUrl(cur)},1500)}else pg("<h2>a serve not reachable</h2><button onclick='A.retry()'>Retry</button>")}}}}
val nv=T(this);val st=Stp(this@M);val rd=Rdr(this);val rc=Rec(this@M);val fr=FrameLayout(this);val vs=listOf<View>(nv,w,st,rd,rc);vs.forEach{fr.addView(it);it.visibility=View.GONE}
fun show(i:Int){vs.forEachIndexed{j,v->v.visibility=if(j==i)View.VISIBLE else View.GONE};vs[i].invalidate()}
// each localhost "box" is its own menu item. The web UI is an SPA, so we PRELOAD it once (onResume, while the selector covers it) and switch views with JS (navpage) → INSTANT nav, no reload. spaR = routes that are SPA views (serve.c: / /jobs /note /tasks /term); others fall back to a full load. native Notes/Note removed — web /note replaces them.
val web=listOf("note" to "/note","task" to "/tasks","term" to "/term","home" to "/","proj" to "/p","op" to "/op","dash" to "/dash","stream" to "/stream","job" to "/jobs","book" to "/book","docs" to "/docs","cloud" to "/cloud","prompt" to "/prompt")
val spaR=listOf("/","/note","/tasks","/term","/jobs")
val items=listOf<Pair<String,()->Unit>>("Native" to {show(0)})+web.map{(l,r)->"a $l" to {show(1);if(loaded&&onSpa&&r in spaR)w.evaluateJavascript("navpage('$r')",null) else {cur="$BASE$r";n=0;w.loadUrl(cur);onSpa=r in spaR}}}+listOf<Pair<String,()->Unit>>("Setup" to {show(2)},"Read" to {show(3)},"Rec" to {show(4)},"Termux" to {startActivity((packageManager.getLaunchIntentForPackage("com.termux")?:Intent().setClassName("com.termux","com.termux.app.TermuxActivity")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))},"homebox" to {startActivity(Intent(this,Cap::class.java))},"Bubble" to {if(android.provider.Settings.canDrawOverlays(this))startService(Intent(this,BubbleService::class.java)) else startActivity(Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION,android.net.Uri.parse("package:$packageName")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))})
val mb=S(this,items,{fr.visibility=View.INVISIBLE},{fr.visibility=View.VISIBLE})
val root=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setBackgroundColor(0xFF000000.toInt())}.also{rt=it}
root.addView(fr,LinearLayout.LayoutParams(-1,0,1f));root.addView(mb,LinearLayout.LayoutParams(-1,-2));setContentView(root);mb.navTo("home");nv.onMenu={mb.open()};openMenu={mb.open()};mb.onLeave={vs.firstOrNull{it.visibility==View.VISIBLE}?.requestFocus()};nav={nm->mb.navTo(nm)};applyNav(intent)}}
// nav selector = the launcher keyboard (launcher.c), translated: bare white letters on pure black, 5-row layout (num row + qwerty + fn keys), keyboard = bottom 52%. Results render BOTTOM-UP so the most-used (navfreq prefs) sits right above the keys; tap a row or type to filter, '>' picks the top match. Closed = a ≡ bar; tap to reopen.
class S(val a:Activity,val it:List<Pair<String,()->Unit>>,val onOp:()->Unit={},val onCl:()->Unit={}):FrameLayout(a){
var i=0;var o=false;var q="";var onLeave:(()->Unit)?=null;var so=0f;var dy=0f;var mv=false;private val slop=android.view.ViewConfiguration.get(a).scaledTouchSlop
private val sp=a.getSharedPreferences("navfreq",0)
private val cnt=HashMap<String,Int>().also{m->for(pr in it)m[pr.first]=sp.getInt(pr.first,0)}
private val KR=listOf("1234567890","qwertyuiop","asdfghjkl","\u0002zxcvbnm\b","\u0001 \n")
private val dens=a.resources.displayMetrics.density
private val fb=Paint().apply{isAntiAlias=true;typeface=Typeface.MONOSPACE;textSize=68f;textAlign=Paint.Align.CENTER}
private val fk=Paint().apply{isAntiAlias=true;typeface=Typeface.MONOSPACE;textSize=46f;textAlign=Paint.Align.CENTER}
private val fq=Paint().apply{isAntiAlias=true;typeface=Typeface.MONOSPACE;textSize=52f;color=0xFFFFFF00.toInt()}
private val fd=Paint().apply{isAntiAlias=true;typeface=Typeface.MONOSPACE;textSize=34f;textAlign=Paint.Align.CENTER}
private val desc=mapOf("native" to "native terminal","note" to "quick notes","task" to "task list","term" to "terminal","home" to "home dashboard","proj" to "projects","op" to "operator view","dash" to "dashboard","stream" to "activity stream","job" to "background jobs","book" to "ebook reader","docs" to "documentation","cloud" to "cloud files","prompt" to "prompt library","setup" to "setup & permissions","read" to "speed reader","rec" to "audio recorder","termux" to "open termux","homebox" to "ssh homebox","bubble" to "floating scroll bubble")
fun open(){o=true;q="";so=0f;onOp();v.isFocusableInTouchMode=true;v.requestFocus();requestLayout();v.invalidate()}
private fun cl(){o=false;q="";onCl();requestLayout();v.invalidate()}
private fun flt():List<Int>{val ql=q.lowercase();val s=it.indices.sortedByDescending{j->cnt[it[j].first]?:0};return if(ql.isEmpty())s else s.filter{j->it[j].first.lowercase().contains(ql)}}
private fun choose(idx:Int){i=idx;val k=it[idx].first;val nc=(cnt[k]?:0)+1;cnt[k]=nc;sp.edit().putInt(k,nc).apply();it[idx].second();cl();onLeave?.invoke()}
fun navTo(name:String):Boolean{val j=it.indexOfFirst{pr->pr.first.equals(name,true)||pr.first.equals("a $name",true)};if(j<0)return false;i=j;it[j].second();if(o)cl();return true}
private val v=object:android.view.View(a){
private fun kbh()=height*0.52f
private fun ky()=height-kbh()
private fun kw()=width/10f
private fun keyAt(x:Float,y:Float):Char?{val k=ky();if(y<k)return null;val r=((y-k)/(kbh()/5)).toInt().coerceIn(0,4);val row=KR[r];val w=kw()
when(r){0,1->{val j=(x/w).toInt();return if(j in row.indices)row[j] else null}
2->{val j=((x-w*0.5f)/w).toInt();return if(j in row.indices)row[j] else null}
3->{if(x<w*1.5f)return row[0];if(x>=w*8.5f)return row[row.length-1];val j=1+((x-w*1.5f)/w).toInt();return if(j in 1 until row.length-1)row[j] else null}
else->{if(x<w*1.5f)return row[0];if(x>=w*8f)return row[2];return row[1]}}}
private fun keyCx(r:Int,j:Int,n:Int):Float{val w=kw();return when(r){0,1->j*w+w/2f;2->w*0.5f+j*w+w/2f;3->if(j==0)w*0.75f else if(j==n-1)(w*8.5f+width)/2f else w*1.5f+(j-1)*w+w/2f;else->if(j==0)w*0.75f else if(j==1)w*4.75f else (w*8f+width)/2f}}
override fun onKeyDown(k:Int,e:android.view.KeyEvent):Boolean{if(!o)return super.onKeyDown(k,e);when(k){android.view.KeyEvent.KEYCODE_DEL->if(q.isNotEmpty())q=q.dropLast(1);android.view.KeyEvent.KEYCODE_ENTER,android.view.KeyEvent.KEYCODE_DPAD_CENTER->{flt().firstOrNull()?.let{choose(it)};return true};android.view.KeyEvent.KEYCODE_BACK->{cl();onLeave?.invoke();return true};else->{val ch=e.unicodeChar;if(ch in 32..126)q+=ch.toChar() else return super.onKeyDown(k,e)}};invalidate();return true}
override fun onDraw(c:Canvas){c.drawColor(0xFF000000.toInt())
if(!o){fb.color=-1;c.drawText("≡  "+it[i].first,width/2f,height*0.6f,fb);return}
val ky=ky();val lh=ky-70f;val rh=120f;val bo=(fb.ascent()+fb.descent())/2f
if(q.isNotEmpty()){fq.textAlign=Paint.Align.LEFT;c.drawText(q,32f,ky-22f,fq)}
val m=flt()
so=so.coerceIn(0f,maxOf(0f,m.size*rh-lh))
for((r,idx) in m.withIndex()){val y=lh-rh*(r+1)+so;if(y<-rh||y>lh)continue
if(r==0){fb.color=-1;c.drawRect(0f,maxOf(0f,y),width.toFloat(),y+rh,fb)};fb.color=if(r==0)0xFF000000.toInt() else -1;c.drawText(it[idx].first,width/2f,y+rh/2f-bo-16f,fb)
fd.color=if(r==0)0xFF444444.toInt() else 0xFF888888.toInt();desc[it[idx].first.lowercase().removePrefix("a ")]?.let{d->c.drawText(d,width/2f,y+rh/2f+34f,fd)}}
val rk=kbh()/5;val bk=(fk.ascent()+fk.descent())/2f
for(r in 0..4){val row=KR[r];val yy=ky+r*rk
for(j in row.indices){val ch=row[j];if(ch==' ')continue;val lbl=when(ch){'\b'->"<";'\n'->">";'\u0001'->"*";'\u0002'->"i";else->ch.toString()};fk.color=-1;c.drawText(lbl,keyCx(r,j,row.length),yy+rk/2f-bk,fk)}}}
override fun onTouchEvent(e:MotionEvent):Boolean{
if(!o){if(e.action==MotionEvent.ACTION_DOWN)open();return true}
val k=ky()
if(e.action==MotionEvent.ACTION_DOWN&&e.y>=k){val ch=keyAt(e.x,e.y);if(ch!=null){when(ch){'\b'->if(q.isNotEmpty())q=q.dropLast(1);'\n'->flt().firstOrNull()?.let{choose(it)};' '->q+=" ";'\u0001'->{};'\u0002'->{};else->q+=ch};so=0f;invalidate()};return true}
when(e.action){MotionEvent.ACTION_DOWN->{dy=e.y;mv=false}
MotionEvent.ACTION_MOVE->{if(!mv&&Math.abs(e.y-dy)>slop)mv=true;if(mv){so+=e.y-dy;dy=e.y;invalidate()}}
MotionEvent.ACTION_UP->if(!mv&&e.y<k){val lh=k-70f;val r=((lh-e.y+so)/120f).toInt();val m=flt();if(r in m.indices)choose(m[r])}}
return true}}
init{addView(v,FrameLayout.LayoutParams(-1,-1))}
override fun onMeasure(ws:Int,hs:Int){val sz=MeasureSpec.getSize(hs);val h=if(o&&sz>0)sz else (56f*dens).toInt();super.onMeasure(ws,MeasureSpec.makeMeasureSpec(h,MeasureSpec.EXACTLY))}}
// homebox = the user's primary ssh device — the main box they work on. Generic alias, not a specific host: each user points "homebox" at their default machine via an ssh entry named homebox. Cap fires captured thoughts to it (a sw homebox) and the button attaches (a ssh homebox).
class Cap:Activity(){
private val ACT="com.aios.a.CAP_RESULT"
private var status:TextView?=null;private var boxhdr:TextView?=null;private var hosts=listOf<String>();private var sr:android.speech.SpeechRecognizer?=null;private var listening=false;private var dbase=""
private val rcv=object:android.content.BroadcastReceiver(){override fun onReceive(c:Context,i:Intent){
val ex=i.extras
var b=ex?.getBundle("com.termux.execute.PLUGIN_RESULT_BUNDLE")?:ex?.getBundle("result")
if(b==null)for(k in ex?.keySet()?:emptySet<String>()){val v=ex?.get(k);if(v is Bundle){b=v;break}}
val o=(b?.getString("stdout")?:"")+(b?.getString("stderr")?:"")
val hi=o.lines().indexOfFirst{it.contains("homebox ->")}
val w=Regex("window (\\S+)").find(o)?.groupValues?.get(1)
val sline=o.lines().lastOrNull{it.trimStart().startsWith("saved")}?.trim()
runOnUiThread{if(hi>=0){boxhdr?.text=o.lines()[hi].replace("->","→")+"  ·tap to switch";hosts=o.lines().drop(hi+1).filter{it.isNotBlank()}}
else status?.text=if(sline!=null)sline else if(w!=null)"✓ window $w" else if(o.isNotBlank())o.trim().takeLast(80) else if(b==null)"keys=["+(ex?.keySet()?.joinToString(",")?:"")+"]" else "bkeys=["+(b?.keySet()?.joinToString(",")?:"")+"]"}}}
private fun txr(vararg a:String){val i=Intent().setClassName("com.termux","com.termux.app.RunCommandService").setAction("com.termux.RUN_COMMAND")
i.putExtra("com.termux.RUN_COMMAND_PATH","/data/data/com.termux/files/usr/bin/bash")
i.putExtra("com.termux.RUN_COMMAND_ARGUMENTS",arrayOf("-c",*a))
i.putExtra("com.termux.RUN_COMMAND_BACKGROUND",true)
i.putExtra("com.termux.RUN_COMMAND_PENDING_INTENT",android.app.PendingIntent.getBroadcast(this,0,Intent(ACT).setPackage(packageName),android.app.PendingIntent.FLAG_UPDATE_CURRENT or android.app.PendingIntent.FLAG_MUTABLE))
try{startForegroundService(i)}catch(e:Exception){try{startService(i)}catch(e2:Exception){}}}
private fun hb(arg:String){if(arg.isEmpty())txr("a ssh hb 2>&1") else txr("a ssh hb \"\$1\" 2>&1","a",arg)}
private fun fire(t:String){if(t.isBlank())return
status?.text="sending…";txr("a sw homebox \"\$1\" 2>&1","a",t)}
override fun onCreate(b:Bundle?){super.onCreate(b)
if(Build.VERSION.SDK_INT>=33)registerReceiver(rcv,android.content.IntentFilter(ACT),Context.RECEIVER_NOT_EXPORTED) else registerReceiver(rcv,android.content.IntentFilter(ACT))
val ll=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;gravity=Gravity.TOP;setBackgroundColor(0xFF000000.toInt())}
val e=EditText(this).apply{setTextColor(-1);setHintTextColor(0xFF888888.toInt());hint="capture → homebox";textSize=22f;setPadding(32,20,24,20);inputType=android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;gravity=Gravity.TOP or Gravity.START;minLines=4;maxLines=12;isVerticalScrollBarEnabled=true}
status=TextView(this).apply{setTextColor(0xFF33FF66.toInt());textSize=16f;setPadding(48,8,48,16);autoLinkMask=android.text.util.Linkify.WEB_URLS;movementMethod=android.text.method.LinkMovementMethod.getInstance()}
boxhdr=TextView(this).apply{setTextColor(0xFF66CCFF.toInt());textSize=15f;setPadding(48,18,48,2);text="homebox → …";setOnClickListener{if(hosts.isEmpty())hb("") else android.app.AlertDialog.Builder(this@Cap).setTitle("homebox = which computer?").setItems(hosts.toTypedArray()){_,wi->hb(hosts[wi])}.show()}}
val btn=Button(this).apply{text="windows";setOnClickListener{status?.text="checking…";txr("a ssh homebox tmux list-windows -t a 2>&1|grep j-|sort -t@ -k2 -n|tail -1|cut -d' ' -f2|grep .||echo NONE")}}
// continuous dictation: partials stream live into the box, each finalized phrase is committed to dbase, and we re-listen on every result/silence so it only stops when you tap ⏹
val mic=Button(this).apply{text="🎤"}
val ri=Intent(android.speech.RecognizerIntent.ACTION_RECOGNIZE_SPEECH).putExtra(android.speech.RecognizerIntent.EXTRA_LANGUAGE_MODEL,android.speech.RecognizerIntent.LANGUAGE_MODEL_FREE_FORM).putExtra(android.speech.RecognizerIntent.EXTRA_PARTIAL_RESULTS,true).putExtra(android.speech.RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS,6000)
fun put(s:String){e.setText(dbase+s);e.setSelection(e.text.length)}
fun reco(){sr=android.speech.SpeechRecognizer.createSpeechRecognizer(this@Cap);sr!!.setRecognitionListener(object:android.speech.RecognitionListener{
override fun onPartialResults(b:Bundle?){b?.getStringArrayList(android.speech.SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.let{put(it)}}
override fun onResults(b:Bundle?){b?.getStringArrayList(android.speech.SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.takeIf{it.isNotBlank()}?.let{dbase=(dbase+it).trimEnd()+" ";put("")};if(listening)sr?.startListening(ri)}
override fun onError(x:Int){if(listening)e.postDelayed({if(listening)sr?.startListening(ri)},150)}
override fun onReadyForSpeech(b:Bundle?){};override fun onBeginningOfSpeech(){};override fun onRmsChanged(r:Float){};override fun onBufferReceived(by:ByteArray?){};override fun onEndOfSpeech(){};override fun onEvent(x:Int,b:Bundle?){}});sr!!.startListening(ri)}
mic.setOnClickListener{
if(listening){listening=false;mic.text="🎤";status?.text="";sr?.destroy();sr=null}
else if(checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)!=android.content.pm.PackageManager.PERMISSION_GRANTED)requestPermissions(arrayOf(android.Manifest.permission.RECORD_AUDIO),7)
else{listening=true;mic.text="⏹";status?.text="🎤 listening… tap ⏹ to stop";dbase=e.text.toString().let{if(it.isEmpty()||it.endsWith(" "))it else "$it "};reco()}}
val save=Button(this).apply{text="send"}
val press=View.OnTouchListener{v,ev->if(ev.action==MotionEvent.ACTION_DOWN)v.alpha=0.45f else if(ev.action==MotionEvent.ACTION_UP||ev.action==MotionEvent.ACTION_CANCEL)v.alpha=1f;false}
listOf(mic,save,btn).forEach{it.setOnTouchListener(press)}
val row=LinearLayout(this).apply{orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER_VERTICAL};row.addView(mic,LinearLayout.LayoutParams(-2,-2));row.addView(save,LinearLayout.LayoutParams(0,-2,1f));row.addView(btn,LinearLayout.LayoutParams(-2,-2))
ll.addView(boxhdr,LinearLayout.LayoutParams(-1,-2));ll.addView(e,LinearLayout.LayoutParams(-1,0,1f));ll.addView(status,LinearLayout.LayoutParams(-1,-2));ll.addView(row,LinearLayout.LayoutParams(-1,-2))
setContentView(ll);e.requestFocus();hb("")
window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE or WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
val go={fire(e.text.toString());e.setText("")}
save.setOnClickListener{go()}}
override fun onDestroy(){listening=false;try{sr?.destroy()}catch(e:Exception){};try{unregisterReceiver(rcv)}catch(e:Exception){};super.onDestroy()}}
class T(c:android.content.Context):android.view.View(c){
private val h=Handler(Looper.getMainLooper())
private var px:IntArray?=null
private var bmp:Bitmap?=null
private var started=false
private external fun nResize(w:Int,hh:Int)
private external fun nFont(d:IntArray)
private external fun nRender(p:IntArray)
private external fun nTouch(a:Int,x:Float,y:Float)
private external fun nKey(d:Int):Int
var onMenu:(()->Unit)?=null
private external fun nStart(ld:String,fd:String)
private external fun nStop()
private external fun nDirty():Boolean
private val tk=object:Runnable{override fun run(){if(bmp!=null&&nDirty())invalidate();h.postDelayed(this,16)}}
init{isFocusable=true;isFocusableInTouchMode=true}
override fun onAttachedToWindow(){super.onAttachedToWindow();h.post(tk);requestFocus()}
override fun onKeyDown(k:Int,e:android.view.KeyEvent):Boolean{val d=when(k){android.view.KeyEvent.KEYCODE_DPAD_LEFT->0;android.view.KeyEvent.KEYCODE_DPAD_RIGHT->1;android.view.KeyEvent.KEYCODE_DPAD_UP->2;android.view.KeyEvent.KEYCODE_DPAD_DOWN->3;android.view.KeyEvent.KEYCODE_DPAD_CENTER,android.view.KeyEvent.KEYCODE_ENTER,android.view.KeyEvent.KEYCODE_BUTTON_A->4;else->return super.onKeyDown(k,e)};if(nKey(d)==0){onMenu?.invoke();return true};invalidate();return true}
override fun onDetachedFromWindow(){if(started)nStop();h.removeCallbacks(tk);super.onDetachedFromWindow()}
override fun onSizeChanged(w:Int,h:Int,ow:Int,oh:Int){if(w*h<1)return
bmp?.recycle();bmp=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888)
px=IntArray(w*h);nResize(w,h)
if(!started){nFont(atlas(56f));nStart(context.applicationInfo.nativeLibraryDir,context.filesDir.absolutePath);started=true}}
override fun onDraw(c:Canvas){val a=px?:return;val b=bmp?:return
nRender(a);b.setPixels(a,0,width,0,0,width,height);c.drawBitmap(b,0f,0f,null)}
override fun onTouchEvent(e:MotionEvent):Boolean{nTouch(e.action and 0xFF,e.x,e.y);invalidate();return true}
private fun atlas(sz:Float):IntArray{
val tf=try{Typeface.createFromAsset(context.assets,"mono.ttf")}catch(e:Exception){Typeface.MONOSPACE}
val p=Paint().apply{textSize=sz;color=-1;isAntiAlias=true;typeface=tf}
val cw=p.measureText("M").toInt();val tb=Rect()
val ch=(-p.ascent()+p.descent()).toInt()+1
val b=Bitmap.createBitmap(cw*95,ch,Bitmap.Config.ARGB_8888)
Canvas(b).let{cc->for(i in 0 until 95){val s=((32+i).toChar()).toString();p.getTextBounds(s,0,1,tb);cc.drawText(s,(i*cw-tb.left).toFloat(),-p.ascent(),p)}}
val r=IntArray(2+cw*95*ch);r[0]=cw;r[1]=ch
b.getPixels(r,2,cw*95,0,0,cw*95,ch);b.recycle();return r}
}
class Stp(val a:Activity):android.view.View(a){
private val p=Paint().apply{color=-1;textSize=60f;textAlign=Paint.Align.CENTER;isAntiAlias=true;typeface=Typeface.MONOSPACE}
private fun go(i:Intent?)=i?.let{a.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))}
private fun bubOn()=(a.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).getRunningServices(99).any{it.service.className==BubbleService::class.java.name}
private val items=listOf<Pair<()->String,()->Unit>>(
{"Set as keyboard"} to{go(Intent(android.provider.Settings.ACTION_INPUT_METHOD_SETTINGS))},
{"Set as launcher"} to{go(Intent(android.provider.Settings.ACTION_HOME_SETTINGS))},
{"Grant usage access"} to{go(Intent(android.provider.Settings.ACTION_USAGE_ACCESS_SETTINGS))},
{"Grant mic permission"} to{a.requestPermissions(arrayOf(android.Manifest.permission.RECORD_AUDIO),1)},
{"Open Shizuku setup"} to{go(a.packageManager.getLaunchIntentForPackage("moe.shizuku.privileged.api")?:Intent(Intent.ACTION_VIEW,android.net.Uri.parse("https://github.com/RikkaApps/Shizuku/releases/latest")))},
{"Enable scroll access"} to{go(Intent(android.provider.Settings.ACTION_ACCESSIBILITY_SETTINGS))},
{"Bubble "+(if(bubOn())"on" else "off")} to{val i=Intent(a,BubbleService::class.java);if(bubOn()){a.getSharedPreferences("bub",0).edit().putBoolean("on",false).apply();a.stopService(i)} else if(android.provider.Settings.canDrawOverlays(a))a.startService(i) else go(Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION,android.net.Uri.parse("package:${a.packageName}")))}
)
private var sel=0
init{isFocusable=true;isFocusableInTouchMode=true}
override fun onDraw(c:Canvas){c.drawColor(0xFF000000.toInt());for((i,e) in items.withIndex()){val y=300f+i*140f;if(i==sel){p.color=-1;c.drawRect(0f,y-78f,width.toFloat(),y+34f,p)};p.color=if(i==sel)0xFF000000.toInt() else -1;c.drawText(e.first(),width/2f,y,p)}}
override fun onKeyDown(k:Int,e:android.view.KeyEvent):Boolean{when(k){android.view.KeyEvent.KEYCODE_DPAD_UP->sel=(sel-1).coerceAtLeast(0);android.view.KeyEvent.KEYCODE_DPAD_DOWN->sel=(sel+1).coerceAtMost(items.size-1);android.view.KeyEvent.KEYCODE_DPAD_CENTER,android.view.KeyEvent.KEYCODE_ENTER,android.view.KeyEvent.KEYCODE_BUTTON_A->items[sel].second();else->return super.onKeyDown(k,e)};invalidate();return true}
override fun onTouchEvent(e:MotionEvent):Boolean{if(e.action==MotionEvent.ACTION_DOWN){val idx=((e.y-240f)/140f).toInt();if(idx in items.indices){sel=idx;items[idx].second();invalidate()}};return true}
}
class Rec(val a:Activity):android.view.View(a){
private val p=Paint().apply{color=-1;textSize=60f;textAlign=Paint.Align.CENTER;isAntiAlias=true;typeface=Typeface.MONOSPACE}
private val pb=Paint().apply{color=-1;textSize=140f;textAlign=Paint.Align.CENTER;isAntiAlias=true;typeface=Typeface.MONOSPACE}
private var msg="tap to record (FLAC 48kHz)"
private val ex=java.util.concurrent.Executors.newSingleThreadExecutor()
private val hh=Handler(Looper.getMainLooper())
@Volatile private var stp=false
private var rec:android.media.AudioRecord?=null;private var t0=0L
private val tk=object:Runnable{override fun run(){if(rec!=null){val s=(System.currentTimeMillis()-t0)/1000;msg=String.format("%02d:%02d",s/60,s%60);invalidate();hh.postDelayed(this,500)}}}
override fun onDraw(c:Canvas){if(rec!=null){c.drawColor(0xFFCC0000.toInt());c.drawText("TAP TO STOP",width/2f,height/2f-40f,pb);c.drawText(msg,width/2f,height/2f+100f,p)}else{c.drawColor(0xFF000000.toInt());c.drawText(msg,width/2f,height/2f,p)}}
override fun onTouchEvent(e:MotionEvent):Boolean{if(e.action==MotionEvent.ACTION_DOWN){if(rec==null)go() else stp=true};return true}
@android.annotation.SuppressLint("MissingPermission")
private fun go(){if(a.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)!=android.content.pm.PackageManager.PERMISSION_GRANTED){a.requestPermissions(arrayOf(android.Manifest.permission.RECORD_AUDIO),1);msg="grant mic perm";invalidate();return}
val SR=48000;val bs=android.media.AudioRecord.getMinBufferSize(SR,android.media.AudioFormat.CHANNEL_IN_MONO,android.media.AudioFormat.ENCODING_PCM_16BIT)*4
try{rec=android.media.AudioRecord(android.media.MediaRecorder.AudioSource.MIC,SR,android.media.AudioFormat.CHANNEL_IN_MONO,android.media.AudioFormat.ENCODING_PCM_16BIT,bs);rec!!.startRecording()}catch(x:Exception){msg="err: ${x.message}";invalidate();return}
t0=System.currentTimeMillis();stp=false;hh.post(tk)
val dir=java.io.File(a.filesDir,"recordings");dir.mkdirs()
val f=java.io.File(dir,"${java.text.SimpleDateFormat("yyyyMMdd-HHmmss").format(java.util.Date())}.flac")
ex.submit{try{
val enc=android.media.MediaCodec.createEncoderByType("audio/flac")
val fmt=android.media.MediaFormat.createAudioFormat("audio/flac",SR,1)
fmt.setInteger(android.media.MediaFormat.KEY_PCM_ENCODING,android.media.AudioFormat.ENCODING_PCM_16BIT)
fmt.setInteger(android.media.MediaFormat.KEY_FLAC_COMPRESSION_LEVEL,5)
enc.configure(fmt,null,null,android.media.MediaCodec.CONFIGURE_FLAG_ENCODE);enc.start()
val out=java.io.FileOutputStream(f);out.write("fLaC".toByteArray())
val bi=android.media.MediaCodec.BufferInfo();val buf=ByteArray(bs);var done=false
while(!done){if(rec!=null){if(stp){rec!!.stop();rec!!.release();rec=null;val ii=enc.dequeueInputBuffer(10000);if(ii>=0)enc.queueInputBuffer(ii,0,0,0,android.media.MediaCodec.BUFFER_FLAG_END_OF_STREAM)}else{val n=rec!!.read(buf,0,bs);if(n>0){val ii=enc.dequeueInputBuffer(10000);if(ii>=0){val ib=enc.getInputBuffer(ii)!!;ib.clear();ib.put(buf,0,n);enc.queueInputBuffer(ii,0,n,System.nanoTime()/1000,0)}}}}
val oi=enc.dequeueOutputBuffer(bi,1000)
if(oi==android.media.MediaCodec.INFO_OUTPUT_FORMAT_CHANGED){val csd=enc.outputFormat.getByteBuffer("csd-0");if(csd!=null){val arr=ByteArray(csd.remaining());csd.get(arr);out.write(byteArrayOf(0x80.toByte(),0,0,arr.size.toByte()));out.write(arr)}}
else if(oi>=0){val ob=enc.getOutputBuffer(oi)!!;val arr=ByteArray(bi.size);ob.position(bi.offset);ob.get(arr)
if((bi.flags and android.media.MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0){out.write(byteArrayOf(0x80.toByte(),0,0,arr.size.toByte()));out.write(arr)}else out.write(arr)
enc.releaseOutputBuffer(oi,false);if((bi.flags and android.media.MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0)done=true}}
enc.stop();enc.release();out.close()
hh.post{msg="saving note...";invalidate()}
txRun(a,"a n \"\$1\"","rec: ${f.absolutePath}")
hh.post{msg="✓ saved ${f.name} → note";invalidate()}
}catch(x:Exception){hh.post{msg="err: ${x.message}";invalidate()}}}
}
}
class Rdr(c:android.content.Context):android.view.View(c){
private val p=Paint().apply{color=-1;textSize=60f;isAntiAlias=true;typeface=Typeface.SERIF}
private val hp=Paint().apply{color=0xFFFFFF00.toInt();textSize=60f;isAntiAlias=true;typeface=Typeface.SERIF}
private val pi=Paint().apply{color=0xFF888888.toInt();textSize=26f;isAntiAlias=true}
private val bp=Paint().apply{color=0xFF222222.toInt()}
private val tp=Paint().apply{color=0xFFFFFFFF.toInt();textSize=32f;textAlign=Paint.Align.CENTER;isAntiAlias=true}
private var tts:android.speech.tts.TextToSpeech?=null
private var sents:List<String> =listOf();private var spoken:Int =-1
private external fun nInit(w:Int,h:Int,cpp:Int)
private external fun nLoad(path:String,startPage:Int):Int
private external fun nPage():String
private external fun nTouch(act:Int,x:Float,y:Float):Int
private external fun nGetPage():Int
private external fun nGetTotal():Int
companion object{init{try{System.loadLibrary("reader")}catch(e:Throwable){}}}
override fun onSizeChanged(w:Int,h:Int,ow:Int,oh:Int){
// average char width measured on a realistic sentence, not "M" which over-estimates
val cw=p.measureText("the quick brown fox jumps over the lazy dog")/43f;val lh=p.fontSpacing
val cpl=((w-80f)/cw).toInt();val lpp=((h-180f)/lh).toInt()
nInit(w,h,cpl*lpp)
val f=java.io.File(context.filesDir,"book.txt");if(f.exists())nLoad(f.absolutePath,0)}
private fun sentencesOf(t:String):List<String> =t.split(Regex("(?<=[.!?])\\s+")).filter{it.isNotBlank()}
override fun onDraw(c:Canvas){
c.drawColor(0xFF000000.toInt())
val t=nPage();sents=sentencesOf(t)
val lh=p.fontSpacing;val mw=width-80f;var y=lh+40f;val btnY=height-110f
for((si,s) in sents.withIndex()){val pp=if(si==spoken)hp else p;val ws=s.split(' ');var ln=StringBuilder()
for(wd in ws){val test=if(ln.isEmpty())wd else "$ln $wd"
if(pp.measureText(test)>mw&&ln.isNotEmpty()){if(y<btnY-lh)c.drawText(ln.toString(),40f,y,pp);y+=lh;ln=StringBuilder(wd)}else ln=StringBuilder(test)}
if(ln.isNotEmpty()){if(y<btnY-lh)c.drawText(ln.toString(),40f,y,pp);y+=lh}
y+=8f}
c.drawRect(0f,btnY,width.toFloat(),height.toFloat(),bp)
c.drawText("▶ speak page",width/2f,height-40f,tp)
c.drawText("${nGetPage()+1}/${nGetTotal()}",40f,height-40f,pi)}
private fun ensureTts(then:()->Unit){if(tts!=null){then();return}
tts=android.speech.tts.TextToSpeech(context,android.speech.tts.TextToSpeech.OnInitListener{st->if(st==android.speech.tts.TextToSpeech.SUCCESS){val vs=tts!!.voices;if(vs!=null)for(v in vs)if(v.name=="en-gb-x-gbd-network"){tts!!.setVoice(v);break};tts!!.setPitch(0.48f)
tts!!.setOnUtteranceProgressListener(object:android.speech.tts.UtteranceProgressListener(){override fun onStart(id:String){spoken=id.toInt();(context as Activity).runOnUiThread{invalidate()}};override fun onDone(id:String){};override fun onError(id:String){}});then()}})}
private fun speakPage(){ensureTts{val ts=tts ?:return@ensureTts;ts.stop();for((i,s) in sents.withIndex()){val q=if(i==0)android.speech.tts.TextToSpeech.QUEUE_FLUSH else android.speech.tts.TextToSpeech.QUEUE_ADD;ts.speak(s,q,null,i.toString())}}}
override fun onTouchEvent(e:MotionEvent):Boolean{if(e.action==MotionEvent.ACTION_DOWN&&e.y>height-110f){speakPage();return true}
if(nTouch(e.action and 0xFF,e.x,e.y)!=0){spoken=-1;tts?.stop();invalidate()};return true}
override fun onDetachedFromWindow(){super.onDetachedFromWindow();tts?.shutdown();tts=null}}
class Sc:android.accessibilityservice.AccessibilityService(){
companion object{var inst:Sc?=null}
override fun onServiceConnected(){inst=this}
override fun onAccessibilityEvent(e:android.view.accessibility.AccessibilityEvent?){}
override fun onInterrupt(){}
override fun onDestroy(){inst=null;super.onDestroy()}
private var bn:android.view.accessibility.AccessibilityNodeInfo?=null;private var ba=0
private fun scan(n:android.view.accessibility.AccessibilityNodeInfo?){if(n==null)return;if(n.isScrollable){val r=android.graphics.Rect();n.getBoundsInScreen(r);val a=r.width()*r.height();if(a>ba){ba=a;bn=n}};for(i in 0 until n.childCount)scan(n.getChild(i))}
fun scroll(fwd:Boolean){bn=null;ba=0;scan(rootInActiveWindow);val n=bn?:return
 val pg=(if(fwd)android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_DOWN else android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_UP).id
 if(!n.performAction(pg))n.performAction(if(fwd)android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD else android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD)}
}
fun startWd(c:Context){val w=Intent(c,Wd::class.java);try{if(Build.VERSION.SDK_INT>=26)c.startForegroundService(w) else c.startService(w)}catch(e:Exception){}}
// Wd = serve keep-alive (reason #2 the apk exists). Durable foreground service: every 8s, probe :1112; if dead, txRun "a serve 1112" — which cold-starts termux if it died (proven: revives in <2s). So serve comes back in the BACKGROUND, before the user opens the app. serve is blocking + double-bind-safe (2nd a serve hits EADDRINUSE, exits) so re-firing when alive is harmless. (poll, not event: android has no cross-process death signal; held-conn liveness needs serve's WS endpoint — upgrade later if the 8s probe ever shows.)
class Wd:android.app.Service(){override fun onBind(i:Intent?):IBinder?=null
override fun onStartCommand(i:Intent?,f:Int,id:Int):Int{val ch="wd";val nm=getSystemService(Context.NOTIFICATION_SERVICE) as android.app.NotificationManager
if(Build.VERSION.SDK_INT>=26)nm.createNotificationChannel(android.app.NotificationChannel(ch,"a",android.app.NotificationManager.IMPORTANCE_MIN))
val nt=android.app.Notification.Builder(this,ch).setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("a serve").build()
if(Build.VERSION.SDK_INT>=34)startForeground(7,nt,android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE) else startForeground(7,nt)
val ctx:Context=this;if(!up){up=true;Thread{while(true){try{val s=java.net.Socket();s.connect(java.net.InetSocketAddress("127.0.0.1",1112),1500);s.close()}catch(e:Exception){txRun(ctx,"a serve 1112")};try{Thread.sleep(8000)}catch(e:Exception){}}}.start()}
return android.app.Service.START_STICKY}
companion object{@Volatile var up=false}}
class Boot:android.content.BroadcastReceiver(){override fun onReceive(c:Context,i:Intent){startWd(c);if(c.getSharedPreferences("bub",0).getBoolean("on",false)){val s=Intent(c,BubbleService::class.java);if(Build.VERSION.SDK_INT>=26)c.startForegroundService(s) else c.startService(s)}}}
class BubbleService:android.app.Service(){
companion object{init{System.loadLibrary("bubble")}}
external fun render(px:IntArray,w:Int,h:Int)
private var vs:List<View> =emptyList()
override fun onBind(i:Intent?):IBinder?=null
private fun queue()=getSharedPreferences("bub",0).getString("q","")!!.split("\n").filter{it.isNotEmpty()&&it!="com.android.settings"&&packageManager.getLaunchIntentForPackage(it)!=null}
private fun fg():String?{val u=getSystemService(Context.USAGE_STATS_SERVICE) as android.app.usage.UsageStatsManager
 val e=System.currentTimeMillis();val s=u.queryUsageStats(android.app.usage.UsageStatsManager.INTERVAL_BEST,e-86400000L,e)?:return null
 return s.maxByOrNull{it.lastTimeUsed}?.packageName}
private fun tmuxNext(){val i=Intent().setClassName("com.termux","com.termux.app.RunCommandService");i.action="com.termux.RUN_COMMAND";i.putExtra("com.termux.RUN_COMMAND_PATH","/data/data/com.termux/files/usr/bin/bash");i.putExtra("com.termux.RUN_COMMAND_ARGUMENTS",arrayOf("-lc",TMUXNEXT));i.putExtra("com.termux.RUN_COMMAND_BACKGROUND",true);try{if(Build.VERSION.SDK_INT>=26)startForegroundService(i) else startService(i)}catch(e:Exception){}}
private fun recent():List<String>{val u=getSystemService(Context.USAGE_STATS_SERVICE) as android.app.usage.UsageStatsManager;val e=System.currentTimeMillis();val st=u.queryUsageStats(android.app.usage.UsageStatsManager.INTERVAL_BEST,e-604800000L,e)?:return emptyList();return st.sortedByDescending{it.lastTimeUsed}.map{it.packageName}.distinct().filter{it!=packageName&&packageManager.getLaunchIntentForPackage(it)!=null}.take(10)}
private fun cycle(){val p=getSharedPreferences("bub",0);var q=queue();if(q.size<2){q=recent();p.edit().putString("q",q.joinToString("\n")).putInt("idx",0).apply()};if(q.isEmpty())return
 val idx=(p.getInt("idx",0)+1)%q.size;p.edit().putInt("idx",idx).apply()
 packageManager.getLaunchIntentForPackage(q[idx])?.let{startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))}}
private fun dismiss(){val q=ArrayList(queue());val i=q.indexOf(fg());if(i>=0){q.removeAt(i);getSharedPreferences("bub",0).edit().putString("q",q.joinToString("\n")).apply()};if(q.isNotEmpty())packageManager.getLaunchIntentForPackage(q[(if(i<0)0 else i)%q.size])?.let{startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))}}
@android.annotation.SuppressLint("ClickableViewAccessibility")
override fun onCreate(){super.onCreate();getSharedPreferences("bub",0).edit().putBoolean("on",true).apply()
val ch="bub";val nm=getSystemService(Context.NOTIFICATION_SERVICE) as android.app.NotificationManager
if(Build.VERSION.SDK_INT>=26)nm.createNotificationChannel(android.app.NotificationChannel(ch,"a",android.app.NotificationManager.IMPORTANCE_MIN))
val nt=android.app.Notification.Builder(this,ch).setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("a bubble").build()
if(Build.VERSION.SDK_INT>=34)startForeground(1,nt,android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE) else startForeground(1,nt)
val s=(resources.displayMetrics.density*40).toInt();val px=IntArray(s*s);render(px,s,s)
val bmp=Bitmap.createBitmap(s,s,Bitmap.Config.ARGB_8888);bmp.setPixels(px,0,s,0,0,s,s)
val wm=getSystemService(Context.WINDOW_SERVICE) as WindowManager
val pf=getSharedPreferences("bub",0);var ox=pf.getInt("ox",(resources.displayMetrics.density*6).toInt());var oy=pf.getInt("oy",0);var dx0=0f;var dy0=0f;var bx=0;var by=0
fun relay(){vs.forEach{val lp=it.layoutParams as WindowManager.LayoutParams;lp.x=ox;lp.y=(it.tag as Int)+oy;wm.updateViewLayout(it,lp)}}
fun mk(dy:Int,tint:Int,lab:String,drag:Boolean,act:()->Unit):View{val fl=FrameLayout(this)
 val w=ImageView(this);w.setImageBitmap(bmp);if(tint!=0)w.setColorFilter(tint,PorterDuff.Mode.SRC_ATOP)
 val tv=TextView(this);tv.text=lab;tv.setTextColor(Color.WHITE);tv.textSize=10f;tv.gravity=Gravity.CENTER
 fl.addView(w,FrameLayout.LayoutParams(s,s));fl.addView(tv,FrameLayout.LayoutParams(s,s))
 fl.setOnTouchListener{_,e->when(e.action){
  MotionEvent.ACTION_DOWN->{w.setColorFilter(Color.WHITE,PorterDuff.Mode.SRC_ATOP);if(drag){dx0=e.rawX;dy0=e.rawY;bx=ox;by=oy}else act();true}
  MotionEvent.ACTION_MOVE->{if(drag){ox=bx+(dx0-e.rawX).toInt();oy=by+(e.rawY-dy0).toInt();relay()};true}
  MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL->{if(tint!=0)w.setColorFilter(tint,PorterDuff.Mode.SRC_ATOP) else w.clearColorFilter();if(drag)pf.edit().putInt("ox",ox).putInt("oy",oy).apply();true}
  else->false}}
 val lp=WindowManager.LayoutParams(s,s,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,PixelFormat.TRANSLUCENT)
 lp.gravity=Gravity.END or Gravity.CENTER_VERTICAL;lp.x=ox;lp.y=dy+oy;fl.tag=dy;wm.addView(fl,lp);return fl}
fun sc(fwd:Boolean){val i=Sc.inst;if(i==null){android.widget.Toast.makeText(this,"Enable scroll access in Setup",android.widget.Toast.LENGTH_SHORT).show();startActivity(Intent(android.provider.Settings.ACTION_ACCESSIBILITY_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))}else i.scroll(fwd)}
val h=(s+16)/2
vs=listOf(mk(-6*h,0xFF1565C0.toInt(),"drop",false){dismiss()},mk(-4*h,0xFF00ACC1.toInt(),"up",false){sc(false)},mk(-2*h,0xFFFB8C00.toInt(),"down",false){sc(true)},mk(0,0xFF2E7D32.toInt(),"home",false){startActivity(Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))},mk(2*h,0,"next",false){cycle()},mk(4*h,0xFF6A1B9A.toInt(),"tmux",false){tmuxNext()},mk(6*h,0xFFAD1457.toInt(),"move",true){})}
override fun onDestroy(){super.onDestroy();val wm=getSystemService(Context.WINDOW_SERVICE) as WindowManager;vs.forEach{try{wm.removeView(it)}catch(e:Exception){}}}}
'''
NC=r'''#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <android/log.h>
#define NC 95
#define MR 120
#define MC 300
typedef struct{uint32_t*px;int cw,ch,aw;}Font;
static Font FN;
typedef struct{uint8_t ch,fg,bg,at;}Cell;
static Cell G[MR*MC];
static int W,H,rows,cols,cx,cy;
static uint8_t cfg=7,cbg=0,cat=0;
static int pty_fd=-1;
static pthread_t rt;
static volatile int dirty=0,run_=0;
static int ctrl_stk=0;
static int pst=0,np=0,pb=0,params[16];
/* row 0 specials: \x01=ESC \x02=TAB \x03=CTRL \x04=LEFT \x05=DOWN \x06=UP \x07=RIGHT \x08=BS \x09=ENTER */
#define NROWS 6
static const char*KR[NROWS]={
 "\x01\x02\x03\x04\x05\x06\x07",
 "1234567890",
 "qwertyuiop",
 "asdfghjkl",
 "zxcvbnm,\x08",
 "-=. \x09"};
static float KB[60][4];
static int nkeys,pressed=-1,selkey=0;
static void kb_rc(int idx,int*pr,int*pc,int*pn){int c=0;for(int r=0;r<NROWS;r++){int n=(int)strlen(KR[r]);if(idx<c+n){*pr=r;*pc=idx-c;*pn=n;return;}c+=n;}*pr=*pc=0;*pn=(int)strlen(KR[0]);}
static int kb_rs(int r){int c=0;for(int i=0;i<r;i++)c+=(int)strlen(KR[i]);return c;}
static uint32_t PAL[16]={
 0xFF000000,0xFFCC0000,0xFF00CC00,0xFFCCCC00,0xFF2244CC,0xFFCC00CC,0xFF00CCCC,0xFFCCCCCC,
 0xFF666666,0xFFFF4444,0xFF44FF44,0xFFFFFF44,0xFF4488FF,0xFFFF44FF,0xFF44FFFF,0xFFFFFFFF};

static void cell_set(int r,int c,uint8_t ch){if(r>=0&&r<rows&&c>=0&&c<cols)G[r*MC+c]=(Cell){ch,cfg,cbg,cat};}
static void scroll1(void){memmove(G,G+MC,sizeof(Cell)*MC*(rows-1));for(int c=0;c<cols;c++)G[(rows-1)*MC+c]=(Cell){' ',cfg,cbg,0};}
static void put_ch(uint8_t c){if(cx>=cols){cx=0;cy++;}if(cy>=rows){scroll1();cy=rows-1;}cell_set(cy,cx,c);cx++;}
static void clmp(void){if(cx<0)cx=0;if(cx>=cols)cx=cols-1;if(cy<0)cy=0;if(cy>=rows)cy=rows-1;}
static void erase(int ra,int ca,int rb,int cb){for(int r=ra;r<=rb&&r<rows;r++){int s=(r==ra)?ca:0,e=(r==rb)?cb:cols-1;for(int c=s;c<=e&&c<cols;c++)cell_set(r,c,' ');}}
static void do_sgr(void){
 if(np==0){cfg=7;cbg=0;cat=0;return;}
 for(int i=0;i<np;i++){int p=params[i];
  if(p==0){cfg=7;cbg=0;cat=0;}
  else if(p==1)cat|=1;
  else if(p==22)cat&=~1;
  else if(p==7){uint8_t t=cfg;cfg=cbg;cbg=t;}
  else if(p>=30&&p<=37)cfg=p-30;
  else if(p==39)cfg=7;
  else if(p>=40&&p<=47)cbg=p-40;
  else if(p==49)cbg=0;
  else if(p>=90&&p<=97)cfg=p-90+8;
  else if(p>=100&&p<=107)cbg=p-100+8;}}
static void do_csi(int f){
 int p1=np>0?params[0]:0,p2=np>1?params[1]:0;
 switch(f){
 case 'H':case 'f':cy=(p1?p1:1)-1;cx=(p2?p2:1)-1;clmp();break;
 case 'A':cy-=p1?p1:1;clmp();break;
 case 'B':cy+=p1?p1:1;clmp();break;
 case 'C':cx+=p1?p1:1;clmp();break;
 case 'D':cx-=p1?p1:1;clmp();break;
 case 'G':cx=(p1?p1:1)-1;clmp();break;
 case 'd':cy=(p1?p1:1)-1;clmp();break;
 case 'J':
  if(p1==0)erase(cy,cx,rows-1,cols-1);
  else if(p1==1)erase(0,0,cy,cx);
  else erase(0,0,rows-1,cols-1);
  break;
 case 'K':
  if(p1==0)erase(cy,cx,cy,cols-1);
  else if(p1==1)erase(cy,0,cy,cx);
  else erase(cy,0,cy,cols-1);
  break;
 case 'm':do_sgr();break;
 case 'X':{int n=p1?p1:1;for(int i=0;i<n&&cx+i<cols;i++)cell_set(cy,cx+i,' ');}break;
 default:break;}}
static void feed(uint8_t b){
 if(pst==0){
  if(b==0x1B)pst=1;
  else if(b=='\r')cx=0;
  else if(b=='\n'){cy++;if(cy>=rows){scroll1();cy=rows-1;}}
  else if(b=='\b'||b==0x7F){if(cx>0)cx--;}
  else if(b=='\t'){cx=(cx+8)&~7;if(cx>=cols)cx=cols-1;}
  else if(b==0x07)return;
  else if(b>=32&&b<127)put_ch(b);
  else if(b>=0x80)put_ch('?');
 }else if(pst==1){
  if(b=='[')pst=2,np=0,pb=0;
  else if(b==']')pst=3;
  else if(b=='='||b=='>'||b=='7'||b=='8'||b=='M')pst=0;
  else if(b=='(')pst=5;
  else pst=0;
 }else if(pst==2){
  if(b>='0'&&b<='9')pb=pb*10+(b-'0');
  else if(b==';'){if(np<15){params[np++]=pb;pb=0;}}
  else if(b=='?'||b=='>'||b=='!')return;
  else if(b>=0x40&&b<=0x7E){if(np<15)params[np++]=pb;do_csi(b);pst=0;}
  else if(b>=0x20&&b<=0x3F)return;
  else pst=0;
 }else if(pst==3){
  if(b==0x07||b==0x9C)pst=0;
  else if(b==0x1B)pst=4;
 }else if(pst==4){pst=0;}
 else if(pst==5){pst=0;}}

static int log_fd=-1;
static void*reader(void*_){
 char buf[4096];
 while(run_){
  int n=(int)read(pty_fd,buf,sizeof(buf));
  if(n<=0)break;
  if(log_fd>=0)(void)!write(log_fd,buf,(size_t)n);
  for(int i=0;i<n;i++)feed((uint8_t)buf[i]);
  dirty=1;}
 return NULL;}
static void*inp_thr(void*_){
 char p[512],b[4096];
 snprintf(p,512,"%s/.inp",getenv("HOME")?getenv("HOME"):"/tmp");
 unlink(p);mkfifo(p,0600);
 while(run_){int f=open(p,O_RDONLY);if(f<0){usleep(100000);continue;}
  int n;while((n=(int)read(f,b,4096))>0)(void)!write(pty_fd,b,(size_t)n);close(f);}
 return NULL;}

static int kbh(float x,float y){for(int i=0;i<nkeys;i++)if(x>=KB[i][0]&&x<KB[i][2]&&y>=KB[i][1]&&y<KB[i][3])return i;return -1;}
static char kch(int idx){int c=0;for(int r=0;r<NROWS;r++){int n=(int)strlen(KR[r]);if(idx<c+n)return KR[r][idx-c];c+=n;}return 0;}
static void compute_kb(void){
 float kh=H*0.55f,ky=H-kh,rh=kh/NROWS;nkeys=0;
 for(int r=0;r<NROWS;r++){
  int n=(int)strlen(KR[r]);float y=ky+r*rh,kw=W/10.0f;
  for(int i=0;i<n;i++){
   float x,w;
   if(r==0){kw=W/(float)n;x=i*kw;w=kw;}
   else if(r==3||r==4){x=kw*0.5f+i*kw;w=kw;}
   else if(r==5){float pos[]={0,kw,2*kw,3*kw,9*kw},wd[]={kw,kw,kw,6*kw,W-9*kw};x=pos[i];w=wd[i];}
   else{x=i*kw;w=kw;}
   KB[nkeys][0]=x;KB[nkeys][1]=y;KB[nkeys][2]=x+w;KB[nkeys][3]=y+rh;nkeys++;}}}
static void compute_grid(void){
 if(!FN.cw||!FN.ch){rows=24;cols=80;return;}
 int kh=(int)(H*0.55f);
 int avail_h=H-kh;
 rows=avail_h/FN.ch;if(rows<1)rows=1;if(rows>MR)rows=MR;
 cols=W/FN.cw;if(cols<20)cols=20;if(cols>MC)cols=MC;
 if(cx>=cols)cx=cols-1;if(cy>=rows)cy=rows-1;
 for(int r=0;r<rows;r++)for(int c=0;c<cols;c++){if(G[r*MC+c].ch==0)G[r*MC+c]=(Cell){' ',7,0,0};}
 if(pty_fd>=0){struct winsize ws={(unsigned short)rows,(unsigned short)cols,0,0};ioctl(pty_fd,TIOCSWINSZ,&ws);}}

static inline void drawch(uint32_t*p,int stride,Font*f,int x,int y,int ch,uint32_t col,uint32_t bg){
 int ci=ch-32;
 uint32_t cr=(col>>16)&0xFF,cg=(col>>8)&0xFF,cb=col&0xFF;
 for(int dy=0;dy<f->ch;dy++){int fy=y+dy;if((unsigned)fy>=(unsigned)H)continue;
  for(int dx=0;dx<f->cw;dx++){int fx=x+dx;if((unsigned)fx>=(unsigned)W)continue;
   uint32_t out=bg;
   if(ci>=0&&ci<NC&&f->px){int sx=ci*f->cw;uint32_t a=f->px[dy*f->aw+sx+dx]>>24;
    if(a){out=0xFF000000|((a*cr/255)<<16)|((a*cg/255)<<8)|(a*cb/255);}}
   p[fy*stride+fx]=out;}}}
static void drawstr(uint32_t*p,int stride,Font*f,const char*s,int x,int y,uint32_t col){
 for(;*s;s++,x+=f->cw)drawch(p,stride,f,x,y,*s,col,0xFF000000);}

static const char*lbl(char c){
 switch((uint8_t)c){
 case 1:return "ESC";case 2:return "TAB";case 3:return "CTL";
 case 4:return "<";case 5:return "v";case 6:return "^";case 7:return ">";
 case 8:return "<-";case 9:return "->";default:{static char b[2];b[0]=c;b[1]=0;return b;}}}

#define JF(ret,name) JNIEXPORT ret JNICALL Java_com_aios_a_T_##name
JF(void,nResize)(JNIEnv*e,jclass c,jint w,jint hh){(void)e;(void)c;W=w;H=hh;compute_kb();compute_grid();dirty=1;}
JF(void,nFont)(JNIEnv*e,jclass c,jintArray arr){(void)c;
 jint*d=(*e)->GetIntArrayElements(e,arr,NULL);
 FN.cw=d[0];FN.ch=d[1];FN.aw=FN.cw*NC;
 int sz=FN.aw*FN.ch;free(FN.px);FN.px=malloc((size_t)sz*4);memcpy(FN.px,d+2,(size_t)sz*4);
 (*e)->ReleaseIntArrayElements(e,arr,d,0);compute_grid();dirty=1;}

JF(void,nRender)(JNIEnv*e,jclass c,jintArray arr){(void)c;
 jint*p=(*e)->GetIntArrayElements(e,arr,NULL);int stride=W;
 for(int i=0;i<W*H;i++)p[i]=0xFF000000;
 if(FN.px){
  int gridh=rows*FN.ch;
  for(int r=0;r<rows;r++)for(int col=0;col<cols;col++){
   Cell cl=G[r*MC+col];
   uint32_t fg=PAL[(cl.at&1)?(cl.fg|8)&15:cl.fg&15],bg=PAL[cl.bg&15];
   int x=col*FN.cw,y=r*FN.ch;
   int iscursor=(r==cy&&col==cx);
   uint32_t abg=iscursor?0xFF44FFAA:bg;
   uint32_t afg=iscursor?0xFF000000:fg;
   drawch((uint32_t*)p,stride,&FN,x,y,cl.ch?cl.ch:' ',afg,abg);
  }
  for(int i=0;i<nkeys;i++){
   int ix=(int)KB[i][0],iy=(int)KB[i][1],iw=(int)(KB[i][2]-KB[i][0]),ih=(int)(KB[i][3]-KB[i][1]);
   const char*lb=lbl(kch(i));int lw=(int)strlen(lb)*FN.cw;
   uint32_t col=(i==pressed)?0xFFFF3333:(ctrl_stk&&lb[0]=='C'&&lb[1]=='T')?0xFFFF8844:0xFFFFFFFF;
   if(i==selkey)for(int dy=0;dy<ih;dy++)for(int dx=0;dx<iw;dx++){int fx=ix+dx,fy=iy+dy;if((unsigned)fx<(unsigned)W&&(unsigned)fy<(unsigned)H)((uint32_t*)p)[fy*stride+fx]=0xFFFFFFFF;}
   int tx=ix+(iw-lw)/2,ty=iy+(ih-FN.ch)/2;
   drawstr((uint32_t*)p,stride,&FN,lb,tx,ty,i==selkey?0xFF000000:col);
  }
  if(gridh<H-(int)(H*0.40f)){/*ok*/}
 }
 (*e)->ReleaseIntArrayElements(e,arr,p,0);dirty=0;}

static void pty_w(const char*s,int n){if(pty_fd>=0)(void)!write(pty_fd,s,n);}
static void send_key(int kidx){
 char c=kch(kidx);uint8_t b;
 switch((uint8_t)c){
 case 1:pty_w("\x1B",1);break;
 case 2:pty_w("\t",1);break;
 case 3:ctrl_stk=!ctrl_stk;dirty=1;return;
 case 4:pty_w("\x1B[D",3);break;
 case 5:pty_w("\x1B[B",3);break;
 case 6:pty_w("\x1B[A",3);break;
 case 7:pty_w("\x1B[C",3);break;
 case 8:pty_w("\x7F",1);break;
 case 9:pty_w("\r",1);break;
 default:
  if(ctrl_stk&&((c>='a'&&c<='z')||(c>='A'&&c<='Z'))){b=(uint8_t)((c|32)-'a'+1);pty_w((char*)&b,1);ctrl_stk=0;dirty=1;}
  else if(ctrl_stk&&c>='@'&&c<='_'){b=(uint8_t)(c-'@');pty_w((char*)&b,1);ctrl_stk=0;dirty=1;}
  else pty_w(&c,1);
 }}

JF(jint,nKey)(JNIEnv*e,jclass c,jint dir){(void)e;(void)c;
 int r,col,n;kb_rc(selkey,&r,&col,&n);
 if(dir==3&&r==NROWS-1)return 0;
 if(dir==0&&col>0)selkey--;
 else if(dir==1&&col<n-1)selkey++;
 else if((dir==2&&r>0)||(dir==3&&r<NROWS-1)){int nr=r+(dir==3?1:-1),nn=(int)strlen(KR[nr]);selkey=kb_rs(nr)+(col<nn?col:nn-1);}
 else if(dir==4)send_key(selkey);
 dirty=1;return 1;}
JF(void,nTouch)(JNIEnv*e,jclass c,jint act,jfloat x,jfloat y){(void)e;(void)c;
 switch(act){
 case 0:pressed=kbh(x,y);if(pressed>=0){send_key(pressed);dirty=1;}break;
 case 1:case 3:pressed=-1;dirty=1;break;
 default:break;}}

JF(void,nStart)(JNIEnv*e,jclass c,jstring libDir,jstring filesDir){(void)c;
 if(pty_fd>=0)return;
 const char*ld=(*e)->GetStringUTFChars(e,libDir,NULL);
 const char*fd=(*e)->GetStringUTFChars(e,filesDir,NULL);
 char path[1024],ti[512],home[512],bin[512];
 snprintf(bin,sizeof(bin),"%s/bin",fd);
 snprintf(path,sizeof(path),"%s:%s:/system/bin:/system/xbin:/product/bin:/apex/com.android.runtime/bin",bin,ld);
 snprintf(ti,sizeof(ti),"%s/terminfo",fd);
 snprintf(home,sizeof(home),"%s",fd);
 (*e)->ReleaseStringUTFChars(e,libDir,ld);
 (*e)->ReleaseStringUTFChars(e,filesDir,fd);
 int m=posix_openpt(O_RDWR|O_NOCTTY);
 if(m<0){__android_log_print(ANDROID_LOG_ERROR,"tlt","openpt:%s",strerror(errno));return;}
 grantpt(m);unlockpt(m);
 char sn[64];if(ptsname_r(m,sn,sizeof(sn))!=0){close(m);return;}
 pid_t p=fork();
 if(p==0){setsid();int sfd=open(sn,O_RDWR);if(sfd<0)_exit(127);
  dup2(sfd,0);dup2(sfd,1);dup2(sfd,2);if(sfd>2)close(sfd);close(m);
  ioctl(0,TIOCSCTTY,0);
  setenv("TERM","xterm-256color",1);
  setenv("TERMINFO",ti,1);
  setenv("HOME",home,1);
  setenv("PATH",path,1);
  setenv("TMPDIR",home,1);
  setenv("TMUX_TMPDIR",home,1);
  setenv("A_SDIR",home,1);
  {char rc[600];snprintf(rc,600,"%s/rclone.conf",home);setenv("RCLONE_CONFIG",rc,1);}
  chdir(home);
  setenv("PS1","$ ",1);
  execlp("tmux","tmux","new-session","-A","-s","apk",(char*)NULL);
  execl("/system/bin/sh","sh",(char*)NULL);_exit(127);}
 pty_fd=m;run_=1;setenv("HOME",home,1);
 {char lp[512];snprintf(lp,512,"%s/.log",home);log_fd=open(lp,O_WRONLY|O_CREAT|O_TRUNC,0600);}
 for(int r=0;r<MR;r++)for(int cc=0;cc<MC;cc++)G[r*MC+cc]=(Cell){' ',7,0,0};
 cx=cy=0;cfg=7;cbg=0;cat=0;pst=0;
 compute_grid();
 pthread_create(&rt,NULL,reader,NULL);
 {pthread_t it;pthread_create(&it,NULL,inp_thr,NULL);pthread_detach(it);}}
JF(void,nStop)(JNIEnv*e,jclass c){(void)e;(void)c;run_=0;if(pty_fd>=0){close(pty_fd);pty_fd=-1;}}
JF(jboolean,nDirty)(JNIEnv*e,jclass c){(void)e;(void)c;return dirty?1:0;}
'''
HKT=r'''package com.aios.a
import android.app.Activity
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.*
import android.content.*
import android.graphics.*
import android.database.sqlite.SQLiteDatabase
import java.io.File
import java.util.concurrent.Executors

class Home : Activity() {
    companion object {
        init { System.loadLibrary("launcher") }
        val P = "爷ye奶nai爸ba妈ma哥ge姐jie弟di妹mei微wei信xin支zhi付fu宝bao淘tao京jing东dong抖dou音yin快kuai手shou美mei团tuan饿e了le么me百bai度du高gao德de地di图tu网wang易yi云yun乐le视shi频pin腾teng讯xun酷ku狗gou虾xia米mi播bo客ke阿a里li巴ba钉ding闲xian鱼yu盒he马ma飞fei猪zhu携xie程cheng去qu哪na儿er艺yi优you芒mang果guo咪mi咕gu喜xi拉la雅ya滴di出chu行xing货huo运yun满man帮bang瓜gua子zi转zhuan闪shan送song达da人ren民min日ri报bao新xin闻wen浪lang博bo豆dou瓣ban知zhi乎hu小xiao红hong书shu得de到dao懒lan饭fan菜cai谱pu下xia厨chu房fang大da众zhong点dian评ping口kou碑bei番fan茄qie钟zhong薄bo荷he唯wei品pin会hui拼pin多duo苏su宁ning当dang国guo贴tie吧ba虎hu扑pu最zui右you即ji刻ke探tan陌mo生sheng交jiao友you她ta他ta约yue爱ai奇qi".let { s -> (0 until s.length).filter { s[it] > '\u4E00' }.associate { i -> s[i] to s.substring(i + 1, (i + 2 until s.length).firstOrNull { s[it] > '\u4E00' } ?: s.length) } }
    }
    private val h = Handler(Looper.getMainLooper())
    private val ex = Executors.newSingleThreadExecutor()
    private val cache by lazy { File(filesDir, "apps.txt") }
    private val db by lazy { SQLiteDatabase.openOrCreateDatabase(File(filesDir, "freq.db"), null).apply { execSQL("CREATE TABLE IF NOT EXISTS f(p TEXT PRIMARY KEY,c INT)"); execSQL("CREATE TABLE IF NOT EXISTS pin(p TEXT PRIMARY KEY)") } }
    private var px: IntArray? = null
    private var cnt = mutableMapOf<String, Int>()
    private var pins = mutableSetOf<String>()
    private lateinit var bmp: Bitmap
    private lateinit var v: View
    private var wrap: android.widget.FrameLayout? = null
    private fun bubOn() = (getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager).getRunningServices(99).any { it.service.className == "com.aios.a.BubbleService" }
    private val la by lazy { getSystemService(Context.LAUNCHER_APPS_SERVICE) as android.content.pm.LauncherApps }
    private val SCF = android.content.pm.LauncherApps.ShortcutQuery.FLAG_MATCH_DYNAMIC or android.content.pm.LauncherApps.ShortcutQuery.FLAG_MATCH_MANIFEST or android.content.pm.LauncherApps.ShortcutQuery.FLAG_MATCH_PINNED
    private external fun nResize(w: Int, h: Int)
    private external fun nFont(id: Int, data: IntArray)
    private external fun nApps(names: Array<String>, pkgs: Array<String>, low: Array<String>, counts: IntArray, pinned: BooleanArray)
    private external fun nRender(pixels: IntArray)
    private external fun nTouch(action: Int, x: Float, y: Float): Int
    private external fun nAnim(): Boolean
    private external fun nResume()
    private external fun nPkg(i: Int): String?
    private external fun nQuery(): String?
    private external fun nTop(): Int
    private external fun nCnt(i: Int, c: Int)
    private external fun nPin(i: Int)

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        if (android.os.Build.VERSION.SDK_INT >= 29) getSystemService(android.app.role.RoleManager::class.java)?.let { rm -> if (rm.isRoleAvailable(android.app.role.RoleManager.ROLE_HOME) && !rm.isRoleHeld(android.app.role.RoleManager.ROLE_HOME)) startActivityForResult(rm.createRequestRoleIntent(android.app.role.RoleManager.ROLE_HOME), 100) }
        db.rawQuery("SELECT p,c FROM f", null).use { while (it.moveToNext()) cnt[it.getString(0)] = it.getInt(1) }
        db.rawQuery("SELECT p FROM pin", null).use { while (it.moveToNext()) pins.add(it.getString(0)) }
        if (cache.exists()) sendApps(cache.readText())
        v = object : View(this@Home) {
            override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
                if (::bmp.isInitialized) bmp.recycle()
                bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888); px = IntArray(w * h)
                nResize(w, h); nFont(0, atlas(72f)); nFont(1, atlas(44f)); nFont(2, atlas(52f))
            }
            override fun onDraw(c: Canvas) {
                val a = px ?: return; if (!::bmp.isInitialized) return
                nRender(a); bmp.setPixels(a, 0, width, 0, 0, width, height)
                c.drawBitmap(bmp, 0f, 0f, null)
            }
            override fun onTouchEvent(e: MotionEvent): Boolean {
                val r = nTouch(e.action and 0xFF, e.x, e.y)
                when {
                    r > 0 -> launch(r - 1)
                    r == -1 -> nQuery()?.let { startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://www.google.com/search?q=" + android.net.Uri.encode(it)))) }
                    r == -4 -> nQuery()?.let { startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("market://search?q=" + android.net.Uri.encode(it)))) }
                    r == -2 -> nPkg(nTop())?.let { startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, android.net.Uri.parse("package:$it"))) }
                    r == -3 -> { val i = nTop(); nPkg(i)?.let { p -> nPin(i); if (!pins.remove(p)) pins.add(p); ex.execute { db.execSQL("DELETE FROM pin WHERE p=?", arrayOf(p)); if (p in pins) db.execSQL("INSERT INTO pin VALUES(?)", arrayOf(p)) } } }
                }
                invalidate()
                if (nAnim()) postInvalidateOnAnimation()
                return true
            }
            override fun computeScroll() { if (nAnim()) { invalidate(); postInvalidateOnAnimation() } }
        }
        wrap = android.widget.FrameLayout(this).apply { setBackgroundColor(0xFF000000.toInt()); addView(v, android.widget.FrameLayout.LayoutParams(-1, -1)) }; setContentView(wrap)
        ex.execute { scan() }
    }

    override fun onResume() { super.onResume(); nResume(); wrap?.setPadding(0, 0, 0, if (bubOn()) (resources.displayMetrics.density * 52).toInt() else 0); if (::bmp.isInitialized) v.invalidate(); ex.execute { scan() } }

    private fun atlas(sz: Float): IntArray {
        val p = Paint().apply { textSize = sz; color = -1; isAntiAlias = true; typeface = Typeface.MONOSPACE; textAlign = Paint.Align.CENTER }
        val cw = p.measureText("M").toInt() + 1; val ch = (-p.ascent() + p.descent()).toInt() + 1
        val b = Bitmap.createBitmap(cw * 95, ch, Bitmap.Config.ARGB_8888)
        Canvas(b).let { c -> for (i in 0 until 95) { c.save(); c.clipRect(i*cw, 0, (i+1)*cw, ch); c.drawText(((32+i).toChar()).toString(), (i*cw + cw/2).toFloat(), -p.ascent(), p); c.restore() } }
        val r = IntArray(2 + cw * 95 * ch); r[0] = cw; r[1] = ch
        b.getPixels(r, 2, cw * 95, 0, 0, cw * 95, ch); b.recycle(); return r
    }

    private fun sendApps(txt: String) {
        val lines = txt.lines().filter { it.contains('\t') }
        val names = Array(lines.size) { lines[it].substringBefore('\t') }
        val pkgs = Array(lines.size) { lines[it].substringAfter('\t') }
        val low = Array(names.size) { names[it].lowercase() + " " + names[it].map { c -> P[c] ?: "" }.joinToString("") }
        nApps(names, pkgs, low, IntArray(names.size) { cnt[pkgs[it]] ?: 0 }, BooleanArray(names.size) { pkgs[it] in pins })
    }

    private fun launch(i: Int) {
        nPkg(i)?.let { pkg ->
            if (pkg.startsWith("~~")) { val p = pkg.substring(2).split("|", limit=2); if (p.size==2) try {
                val q = android.content.pm.LauncherApps.ShortcutQuery().setPackage(p[0]).setShortcutIds(listOf(p[1])).setQueryFlags(SCF)
                la.getShortcuts(q, android.os.Process.myUserHandle())?.firstOrNull()?.let { la.startShortcut(it, null, null) }
            } catch (e: Exception) {} ; return }
            if (pkg.startsWith("~")) { try { startActivity(Intent(pkg.substring(1)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (e: Exception) {} ; return }
            val c = (cnt[pkg] ?: 0) + 1; cnt[pkg] = c; nCnt(i, c)
            ex.execute { db.execSQL("INSERT OR REPLACE INTO f VALUES(?,?)", arrayOf(pkg, c)) }
            getSharedPreferences("bub",0).let{p->val q=p.getString("q","")!!.split("\n").filter{it.isNotEmpty()};if(pkg !in q)p.edit().putString("q",(q+pkg).joinToString("\n")).apply()}
            packageManager.getLaunchIntentForPackage(pkg)?.let { startActivity(it) }
        }
    }

    private fun scan() {
        val pm = packageManager; val me = android.os.Process.myUserHandle()
        val real = pm.queryIntentActivities(Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER), 0).flatMap { ri ->
            val lbl = ri.loadLabel(pm).toString(); val pk = ri.activityInfo.packageName
            val sc = try { la.getShortcuts(android.content.pm.LauncherApps.ShortcutQuery().setPackage(pk).setQueryFlags(SCF), me) ?: emptyList() } catch (e: Exception) { emptyList() }
            listOf(lbl to pk) + sc.map { ((if (pk == packageName) "" else "$lbl > ") + (it.shortLabel?.toString() ?: it.id)) to ("~~" + pk + "|" + it.id) }
        }
        val sys = android.provider.Settings::class.java.fields.filter { it.name.startsWith("ACTION_") && it.type == String::class.java }.mapNotNull { f ->
            val a = try { f.get(null) as String } catch (e: Exception) { return@mapNotNull null }
            if (pm.resolveActivity(Intent(a), 0) == null) return@mapNotNull null
            val raw = f.name.removePrefix("ACTION_").removeSuffix("_SETTINGS")
            if (raw.isEmpty()) return@mapNotNull null
            raw.split("_").joinToString(" ") { it.lowercase().replaceFirstChar(Char::uppercase) } to "~$a"
        }
        val txt = (real + listOf("Hotspot" to "~android.settings.TETHER_SETTINGS") + sys).joinToString("\n") { "${it.first}\t${it.second}" }
        val old = if (cache.exists()) cache.readText() else ""
        if (txt != old) { cache.writeText(txt); h.post { sendApps(txt); if (::bmp.isInitialized) v.invalidate() } }
    }
}
'''
LC=r'''#include <jni.h>
#include <android/bitmap.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NA 1024
#define NM 96
#define NC 95

typedef struct { uint32_t* px; int cw, ch, aw; } Font;
static Font F[3];
static int W, H;

static struct { char n[NM]; char p[128]; char l[NM]; int c, pin; } A[NA];
static int na, srt[NA], flt[NA], nf;
static char Q[64];
static int ql, scr, pressed, drag, sel;
static float sv, ly, ty;

static const char* KR[] = {"1234567890","qwertyuiop","asdfghjkl","\x02zxcvbnm\b","\x01 \n"};
static float KB[50][4];
static int nk;

static void dosort() {
    for (int i = 0; i < na; i++) srt[i] = i;
    for (int i = 1; i < na; i++) {
        int k = srt[i], v = A[k].pin * 1000000 + A[k].c, j = i - 1;
        while (j >= 0 && A[srt[j]].pin * 1000000 + A[srt[j]].c < v) { srt[j+1] = srt[j]; j--; }
        srt[j+1] = k;
    }
}

static void dofilter() {
    nf = 0; scr = 0; sv = 0;
    if (!ql) { for (int i = 0; i < na; i++) flt[i] = srt[i]; nf = na; return; }
    char q[64]; for (int i = 0; i < ql; i++) q[i] = Q[i] | 32; q[ql] = 0;
    for (int i = 0; i < na; i++) if (strstr(A[srt[i]].l, q)) flt[nf++] = srt[i];
}

static void computekb() {
    float kw = W / 10.0f, kbh = H * 0.52f, ky = H - kbh, rh = kbh / 5; nk = 0;
    for (int r = 0; r < 5; r++) {
        int n = strlen(KR[r]); float y = ky + r * rh;
        for (int i = 0; i < n; i++) {
            float x = i * kw, w = kw;
            if (r == 2) x = kw * 0.5f + i * kw;
            else if (r == 3) {
                if (i == 0) { x = 0; w = kw * 1.5f; }
                else if (i == n-1) { x = kw * 8.5f; w = W - x; }
                else { x = kw * 1.5f + (i-1) * kw; }
            } else if (r == 4) {
                if (i == 0) { x = 0; w = kw * 1.5f; }
                else if (i == 1) { x = kw * 1.5f; w = kw * 6.5f; }
                else { x = kw * 8; w = W - x; }
            }
            KB[nk][0] = x; KB[nk][1] = y; KB[nk][2] = x + w; KB[nk][3] = y + rh; nk++;
        }
    }
}

static int kbhit(float x, float y) {
    for (int i = 0; i < nk; i++)
        if (x >= KB[i][0] && x < KB[i][2] && y >= KB[i][1] && y < KB[i][3]) return i;
    return -1;
}

static char kbchar(int idx) {
    int c = 0;
    for (int r = 0; r < 5; r++) { int n = strlen(KR[r]); if (idx < c + n) return KR[r][idx - c]; c += n; }
    return 0;
}

static inline void drawch(uint32_t* px, int stride, Font* f, int x, int y, int ch, uint32_t col) {
    int ci = ch - 32; if (ci < 0 || ci >= NC) return;
    int sx = ci * f->cw;
    uint32_t cr = col >> 16 & 0xFF, cg = col >> 8 & 0xFF, cb = col & 0xFF;
    for (int dy = 0; dy < f->ch; dy++) {
        int fy = y + dy; if ((unsigned)fy >= (unsigned)H) continue;
        for (int dx = 0; dx < f->cw; dx++) {
            int fx = x + dx; if ((unsigned)fx >= (unsigned)W) continue;
            uint32_t a = f->px[dy * f->aw + sx + dx] >> 24;
            if (a) px[fy * stride + fx] = 0xFF000000 | (a*cr/255 << 16) | (a*cg/255 << 8) | a*cb/255;
        }
    }
}

static void drawstr(uint32_t* px, int stride, Font* f, const char* s, int x, int y, uint32_t col) {
    for (; *s; s++, x += f->cw) drawch(px, stride, f, x, y, *s, col);
}

static void drawcenter(uint32_t* px, int stride, Font* f, const char* s, int y, uint32_t col) {
    drawstr(px, stride, f, s, (W - (int)strlen(s) * f->cw) / 2, y, col);
}

#define JF(ret, name) JNIEXPORT ret JNICALL Java_com_aios_a_Home_##name

JF(void, nResize)(JNIEnv* e, jclass c, jint w, jint h) { W = w; H = h; computekb(); }

JF(void, nFont)(JNIEnv* e, jclass c, jint id, jintArray arr) {
    jint* d = (*e)->GetIntArrayElements(e, arr, NULL);
    int cw = d[0], ch = d[1];
    F[id].cw = cw; F[id].ch = ch; F[id].aw = cw * NC;
    free(F[id].px); F[id].px = malloc(F[id].aw * ch * 4);
    memcpy(F[id].px, d + 2, F[id].aw * ch * 4);
    (*e)->ReleaseIntArrayElements(e, arr, d, 0);

}

static void gs(JNIEnv* e, jobjectArray a, int i, char* d, int m) {
    jstring s = (*e)->GetObjectArrayElement(e, a, i);
    const char* c = (*e)->GetStringUTFChars(e, s, NULL);
    strncpy(d, c, m-1); d[m-1] = 0;
    (*e)->ReleaseStringUTFChars(e, s, c); (*e)->DeleteLocalRef(e, s);
}

JF(void, nApps)(JNIEnv* e, jclass c, jobjectArray names, jobjectArray pkgs, jobjectArray low, jintArray cnts, jbooleanArray pins) {
    na = (*e)->GetArrayLength(e, names); if (na > NA) na = NA;
    jint* cn = (*e)->GetIntArrayElements(e, cnts, NULL);
    jboolean* pn = (*e)->GetBooleanArrayElements(e, pins, NULL);
    for (int i = 0; i < na; i++) { gs(e, names, i, A[i].n, NM); gs(e, pkgs, i, A[i].p, 128); gs(e, low, i, A[i].l, NM); A[i].c = cn[i]; A[i].pin = pn[i]; }
    (*e)->ReleaseIntArrayElements(e, cnts, cn, 0);
    (*e)->ReleaseBooleanArrayElements(e, pins, pn, 0);
    dosort(); dofilter();

}

JF(void, nRender)(JNIEnv* e, jclass c, jintArray arr) {
    jint* p = (*e)->GetIntArrayElements(e, arr, NULL);
    int stride = W;
    for (int i = 0; i < W * H; i++) p[i] = 0xFF000000;

    int ky = H - H * 52 / 100, lh = ky - 70, rh = 120;

    if (!drag && fabsf(sv) > 0.5f) { scr += (int)sv; sv *= 0.95f; } else if (!drag) sv = 0;
    int mx = nf * rh - lh; if (mx < 0) mx = 0;
    if (scr < 0) scr = 0; if (scr > mx) scr = mx;

    if (ql > 0 && F[2].px) { char q[65]; memcpy(q, Q, ql); q[ql] = 0; drawstr((uint32_t*)p, stride, &F[2], q, 32, ky - 60, 0xFFFF00); }

    if (F[0].px) {
        if (nf == 0 && ql > 0) {
            static const char* LBL[2] = {"Google: ", "Play Store: "};
            for (int j = 0; j < 2; j++) { char buf[80]; int l = (int)strlen(LBL[j]);
                memcpy(buf, LBL[j], l); memcpy(buf+l, Q, ql); buf[l+ql] = 0;
                drawcenter((uint32_t*)p, stride, &F[0], buf, lh - rh*(j+1) + rh/2 - F[0].ch/2, 0xFFFF00); }
        } else for (int i = 0; i < nf; i++) {
            int y = lh - rh * (i+1) + scr;
            if (y < -rh || y > lh) continue;
            int idx = flt[i]; char buf[NM+3]; int off = 0;
            if (A[idx].pin) { buf[0] = '*'; buf[1] = ' '; off = 2; }
            strncpy(buf+off, A[idx].n, NM); buf[off+NM-1] = 0;
            { uint32_t nc = i==sel ? 0x000000 : 0xFFFFFF;
              if (i==sel) for (int yy=y<0?0:y; yy<y+rh && (unsigned)yy<(unsigned)H; yy++) for (int xx=0; xx<W; xx++) ((uint32_t*)p)[yy*stride+xx]=0xFFFFFFFF;
              if (A[idx].p[0]=='~' && A[idx].p[1]!='~') { int nw=(int)strlen(buf)*F[0].cw, tw=13*F[1].cw, sx=(W-nw-20-tw)/2; if (sx<0) sx=0;
                drawstr((uint32_t*)p, stride, &F[0], buf, sx, y+rh/2-F[0].ch/2, nc);
                drawstr((uint32_t*)p, stride, &F[1], "settings menu", sx+nw+20, y+rh/2-F[1].ch/2, i==sel?0x000000:0x808080); }
              else drawcenter((uint32_t*)p, stride, &F[0], buf, y+rh/2-F[0].ch/2, nc); }
        }
    }

    if (F[1].px) {
        int ki = 0;
        for (int r = 0; r < 5; r++) for (int i = 0; KR[r][i]; i++, ki++) {
            char ch = KR[r][i]; if (ch == ' ') continue;
            uint32_t col = ki == pressed ? 0xFF3333 : 0xFFFFFF;
            char lbl = ch;
            if (ch == '\b') lbl = '<'; else if (ch == '\n') lbl = '>';
            else if (ch == '\x01') lbl = '*'; else if (ch == '\x02') lbl = 'i';
            float cx = (KB[ki][0] + KB[ki][2]) / 2, cy = (KB[ki][1] + KB[ki][3]) / 2;
            drawch((uint32_t*)p, stride, &F[1], cx - F[1].cw/2, cy - F[1].ch/2, lbl, col);
        }
    }
    (*e)->ReleaseIntArrayElements(e, arr, p, 0);
}

JF(jint, nTouch)(JNIEnv* e, jclass c, jint act, jfloat x, jfloat y) {
    int ky = H - H * 52 / 100, lh = ky - 70, rh = 120; float edge = W * 0.15f;
    switch (act) {
    case 0:
        if (y >= ky) {
            pressed = kbhit(x, y);
            if (pressed >= 0) {
                char ch = kbchar(pressed); int pnf = nf, typed = 0;
                if (ch == '\b') { if (ql > 0) ql--; }
                else if (ch == '\n') { if (nf > 0) return flt[sel]+1; if (ql > 0) return -1; }
                else if (ch == '\x01') { if (nf > 0) return -3; }
                else if (ch == '\x02') { if (nf > 0) return -2; }
                else if (ql < 63) { Q[ql++] = ch; typed = 1; }
                dofilter();
                sel = typed && nf == pnf ? sel + 1 : 0;
                if (sel >= nf) sel = nf ? nf - 1 : 0;
            }
        } else if (y < lh) {
            if (x < edge || x > W - edge) { drag = 1; ly = y; ty = y; sv = 0; }
            else { int i = (int)((lh - y + scr) / rh);
                if (i >= 0 && i < nf) return flt[i]+1;
                if (nf == 0 && ql > 0) return i == 1 ? -4 : -1; }
        } break;
    case 1: case 3: pressed = -1; drag = 0; break;
    case 2: if (drag) { sv = y - ly; scr += (int)(y - ly); ly = y;
            int mx = nf * rh - lh; if (mx < 0) mx = 0;
            if (scr < 0) scr = 0; if (scr > mx) scr = mx; } break;
    }
    return 0;
}

JF(jboolean, nAnim)(JNIEnv* e, jclass c) { return fabsf(sv) > 0.5f; }
JF(void, nResume)(JNIEnv* e, jclass c) { ql = 0; scr = 0; pressed = -1; sv = 0; drag = 0; sel = 0; dofilter(); }
JF(jstring, nPkg)(JNIEnv* e, jclass c, jint i) { return (i >= 0 && i < na) ? (*e)->NewStringUTF(e, A[i].p) : NULL; }
JF(jstring, nQuery)(JNIEnv* e, jclass c) { char b[65]; memcpy(b, Q, ql); b[ql] = 0; return (*e)->NewStringUTF(e, b); }
JF(jint, nTop)(JNIEnv* e, jclass c) { return nf > 0 ? flt[sel] : -1; }
JF(void, nCnt)(JNIEnv* e, jclass c, jint i, jint v) { if (i >= 0 && i < na) { A[i].c = v; dosort(); dofilter(); } }
JF(void, nPin)(JNIEnv* e, jclass c, jint i) { if (i >= 0 && i < na) { A[i].pin = !A[i].pin; dosort(); dofilter(); } }
'''
TXML=r'''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="T" parent="android:Theme.Material.NoActionBar">
        <item name="android:colorBackground">#000</item>
        <item name="android:textColor">#fff</item>
    </style>
</resources>
'''
IKT=r'''package com.aios.a
import android.inputmethodservice.InputMethodService
import android.graphics.*
import android.view.*
import android.os.Handler
import android.os.Looper
import android.speech.*

object NativeKB {
    init { System.loadLibrary("keyboard") }
    external fun init(w: Int, h: Int)
    external fun setRowFudge(row: Int, fudge: Float)
    external fun setHFudge(fudge: Float)
    external fun getRowCount(): Int
    external fun onTouch(action: Int, x: Float, y: Float): Int
    external fun checkHold(): Int
    external fun toggleMode()
    external fun toggleShift()
    external fun clearShift()
    external fun getMode(): Int
    external fun getShift(): Int
    external fun getPressed(): Int
    external fun getHFudge(): Float
    external fun getRowFudge(row: Int): Float
    external fun getBounds(out: FloatArray)
    external fun getLabels(): String
}

// Clip: persisted history of everything committed via the keyboard (dictation finals, typed lines)
// + whatever is on the system clipboard when opened. Newest-first, capped, survives IME restarts so
// nothing said/typed/copied is ever lost. The 📋 key (left of mic) shows it; tap an entry to re-insert.
object Clip {
    private val items = ArrayList<String>(); private var f: java.io.File? = null
    fun init(c: android.content.Context) { if (f != null) return; f = java.io.File(c.filesDir, "clip.txt"); try { f!!.readText().split("\u0000").forEach { if (it.isNotBlank()) items.add(it) } } catch (e: Exception) {} }
    fun add(s: String) { val t = s.trim(); if (t.isEmpty() || t == items.firstOrNull()) return; items.remove(t); items.add(0, t); while (items.size > 40) items.removeAt(items.size - 1); try { f?.writeText(items.joinToString("\u0000")) } catch (e: Exception) {} }
    fun list(): List<String> = items
}
// Dict: shared continuous dictation (both IMEs) — partials stream as composing text, finals commit.
// API33+ uses an on-device (Gboard-speed) SpeechRecognizer in EXTRA_SEGMENTED_SESSION mode: each
// sentence arrives via onSegmentResults and commits as completed while the session keeps listening
// (no per-utterance restart, no overwrite); pre-33 falls back to the old restart loop. Raw editors (inputType
// TYPE_NULL: terminals like termux) can't compose — stream append-only tails there; NEVER delete
// (termux deletes = async key events that can eat pre-existing content). Revisions may dup a few
// boundary chars; content is never lost.
class Dict(private val svc: InputMethodService, private val ui: () -> Unit) {
    var listening = false; private var sr: SpeechRecognizer? = null; private var sent = 0
    private val ic get() = svc.currentInputConnection
    private val raw get() = svc.currentInputEditorInfo?.inputType == android.text.InputType.TYPE_NULL
    private fun put(s: String, fin: Boolean) {
        if (raw) { val t = if (fin) "$s " else s
            if (t.length > sent) { ic?.commitText(t.substring(sent), 1); sent = t.length }
            if (fin) sent = 0 }
        else { if (fin) ic?.commitText("$s ", 1) else ic?.setComposingText(s, 1); sent = if (fin) 0 else s.length }
        if (fin) { Clip.add(s); ui() }  // refresh the live clipboard list so the new sentence shows the instant it commits
    }
    private fun seal() { if (sent > 0) { ic?.finishComposingText(); ic?.commitText(" ", 1); sent = 0 } }
    fun toggle() { if (listening) stop() else start() }
    fun stop() { listening = false; sent = 0; ic?.finishComposingText(); try { sr?.destroy() } catch (e: Exception) {}; sr = null; ui() }
    private fun start() {
        if (svc.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) { svc.startActivity(android.content.Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, android.net.Uri.parse("package:${svc.packageName}")).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)); return }
        Clip.init(svc); listening = true; ui()
        val seg = android.os.Build.VERSION.SDK_INT >= 33  // API33+: segmented session emits each sentence via onSegmentResults & keeps going — commits as completed, no restart, no overwrite
        val ri = android.content.Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM).putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true).putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
        if (seg) ri.putExtra(RecognizerIntent.EXTRA_SEGMENTED_SESSION, RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS).putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 2000).putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_MINIMUM_LENGTH_MILLIS, 1000).putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_POSSIBLY_COMPLETE_SILENCE_LENGTH_MILLIS, 2000).putExtra(RecognizerIntent.EXTRA_ENABLE_FORMATTING, RecognizerIntent.FORMATTING_OPTIMIZE_QUALITY)
        else ri.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 6000)
        sr = if (android.os.Build.VERSION.SDK_INT >= 31 && SpeechRecognizer.isOnDeviceRecognitionAvailable(svc)) SpeechRecognizer.createOnDeviceSpeechRecognizer(svc) else SpeechRecognizer.createSpeechRecognizer(svc)  // on-device neural engine = Gboard speed, no network round-trip
        sr!!.setRecognitionListener(object : RecognitionListener {
            override fun onPartialResults(b: android.os.Bundle?) { b?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.takeIf { it.isNotBlank() }?.let { put(it, false) } }
            override fun onSegmentResults(b: android.os.Bundle) { b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.takeIf { it.isNotBlank() }?.let { put(it, true) } }
            override fun onEndOfSegmentedSession() { seal(); if (listening) sr?.startListening(ri) }
            override fun onResults(b: android.os.Bundle?) { val r = b?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull(); if (r != null && r.isNotBlank()) put(r, true) else seal(); if (!seg && listening) sr?.startListening(ri) }
            override fun onError(c: Int) { seal(); if (listening) Handler(Looper.getMainLooper()).postDelayed({ if (listening) sr?.startListening(ri) }, 150) }
            override fun onEndOfSpeech() {}; override fun onReadyForSpeech(b: android.os.Bundle?) {}; override fun onBeginningOfSpeech() {}; override fun onRmsChanged(r: Float) {}; override fun onBufferReceived(b: ByteArray?) {}; override fun onEvent(c: Int, b: android.os.Bundle?) {}
        }); sr!!.startListening(ri)
    }
}

class InstantNdkService : InputMethodService(), android.view.textservice.SpellCheckerSession.SpellCheckerSessionListener {
    private lateinit var kbView: NdkKeyboardView
    private var root: android.widget.LinearLayout? = null
    private var sc: android.view.textservice.SpellCheckerSession? = null
    private var fix: String? = null; private var fixWord = ""; private var skip = ""; private var word = ""; private var typed = ""

    override fun onEvaluateFullscreenMode() = false
    override fun onGetSuggestions(r: Array<out android.view.textservice.SuggestionsInfo>?) { r?.firstOrNull()?.let { if (it.suggestionsCount > 0) fix = it.getSuggestionAt(0) } }
    override fun onGetSentenceSuggestions(r: Array<out android.view.textservice.SentenceSuggestionsInfo>?) {}

    override fun onCreateInputView(): View { Clip.init(this); sc = (getSystemService(android.content.Context.TEXT_SERVICES_MANAGER_SERVICE) as? android.view.textservice.TextServicesManager)?.newSpellCheckerSession(null, null, this, true); kbView = NdkKeyboardView(this); root = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.VERTICAL; setBackgroundColor(0xFF000000.toInt()); addView(kbView) }; return root!! }
    override fun onStartInputView(info: android.view.inputmethod.EditorInfo?, restarting: Boolean) { super.onStartInputView(info, restarting)
        fix = null; fixWord = ""; skip = ""; word = "" }
    override fun onFinishInputView(f: Boolean) { kbView.rec.stop(); super.onFinishInputView(f) }

    fun sendChar(c: Char) = when (c) {
        '\b' -> { word = word.dropLast(1); typed = typed.dropLast(1); currentInputConnection?.deleteSurroundingText(1, 0) }
        '\n' -> { word = ""; if (typed.isNotBlank()) Clip.add(typed); typed = ""; val a = currentInputEditorInfo?.imeOptions?.and(android.view.inputmethod.EditorInfo.IME_MASK_ACTION) ?: 0
            if (a != 0 && a != android.view.inputmethod.EditorInfo.IME_ACTION_NONE) currentInputConnection?.performEditorAction(a)
            else currentInputConnection?.commitText("\n", 1) }
        ' ' -> { if (fix != null && word.equals(fixWord, true) && word.lowercase() != skip) { currentInputConnection?.deleteSurroundingText(word.length, 0); currentInputConnection?.commitText("$fix ", 1); skip = word.lowercase() }
            else currentInputConnection?.commitText(" ", 1); word = ""; fix = null; typed += " "; null }
        '\u0001' -> { NativeKB.toggleMode(); kbView.invalidate(); null }
        '\u0002' -> { NativeKB.toggleShift(); kbView.invalidate(); null }
        else -> { val out = if (NativeKB.getShift() == 1 && c in 'a'..'z') c.uppercaseChar() else c
            currentInputConnection?.commitText(out.toString(), 1)
            if (NativeKB.getShift() == 1 && c in 'a'..'z') { NativeKB.clearShift(); kbView.invalidate() }
            word += out; typed += out; if (word.length >= 2) { fixWord = word; fix = null; sc?.getSuggestions(android.view.textservice.TextInfo(word), 3) }; null }
    }
}

class NdkKeyboardView(private val svc: InstantNdkService) : View(svc) {
    private val prefs = android.preference.PreferenceManager.getDefaultSharedPreferences(svc)
    private val bgPaint = Paint().apply { color = 0xFF000000.toInt() }
    private val keyPaint = Paint().apply { color = 0xFF000000.toInt() }
    private val pressPaint = Paint()
    private val textPaint = Paint().apply { color = Color.WHITE; textSize = 48f; textAlign = Paint.Align.CENTER; isAntiAlias = true }
    private fun updatePressPaint() { pressPaint.color = if (prefs.getBoolean("debug_red", true)) 0xFFFF0000.toInt() else 0xFF606060.toInt() }
    private val bounds = FloatArray(48 * 4)
    private val handler = Handler(Looper.getMainLooper())
    private val holdCheck = object : Runnable { override fun run() {
        val c = NativeKB.checkHold()
        if (c == '\b'.code) { svc.sendChar('\b'); invalidate() }
        else if (c == 'S'.code) svc.startActivity(android.content.Intent(svc, SettingsActivity::class.java).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK))
        if (NativeKB.getPressed() >= 0) handler.postDelayed(this, 16)
    }}

    private val toolbarH = 0f
    val rec = Dict(svc) { invalidate() }
    // clipboard history: 📋 key — OR active dictation — fills the keyboard area with a live tappable list
    // (newest-first, read fresh each draw). Each dictated sentence appends the instant it commits, so you
    // watch it fill as you talk while the text field fills above. Row 0 = ⏹ stop (dictating) / ✕ close; tap an entry to re-insert.
    var clipMode = false
    private val clipPaint = Paint().apply { color = Color.WHITE; textSize = 38f; isAntiAlias = true }
    private val rowH get() = resources.displayMetrics.density * 50
    private fun cmgr() = svc.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
    private fun loadClip() { try { cmgr().primaryClip?.getItemAt(0)?.coerceToText(svc)?.toString()?.let { Clip.add(it) } } catch (e: Exception) {} }
    private fun drawClip(canvas: Canvas) { canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)
        canvas.drawText(if (rec.listening) "⏹  stop" else "✕  close", 24f, rowH * 0.64f, clipPaint)
        if (rec.listening) canvas.drawText("⏭  next session", width * 0.45f, rowH * 0.64f, clipPaint)  // voice-tmux: switch session without stopping dictation
        val items = Clip.list()
        for (i in 0 until minOf(items.size, (height / rowH).toInt() - 1)) canvas.drawText(items[i].replace("\n", " ").take(46), 24f, rowH * (i + 1) + rowH * 0.64f, clipPaint) }
    private fun tapClip(x: Float, y: Float) { val row = (y / rowH).toInt()
        if (row == 0) { if (rec.listening) { if (x > width * 0.45f) txRun(svc, TMUXNEXT) else rec.stop() } else clipMode = false }
        else { val items = Clip.list(); if (row - 1 < items.size) { val s = items[row - 1]; svc.currentInputConnection?.commitText(s, 1); try { cmgr().setPrimaryClip(android.content.ClipData.newPlainText("a", s)) } catch (e: Exception) {} }; if (!rec.listening) clipMode = false }
        invalidate() }
    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        NativeKB.init(w, h - toolbarH.toInt())
        for (r in 0 until 5) NativeKB.setRowFudge(r, prefs.getInt("fudge$r", 20).toFloat())
        NativeKB.setHFudge(prefs.getInt("hfudge", 10).toFloat())
        updatePressPaint()
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); updatePressPaint() }
    override fun onMeasure(ws: Int, hs: Int) {
        val w = MeasureSpec.getSize(ws)
        val pct = prefs.getInt("kb_height", 95).coerceIn(20, 200) / 100f
        setMeasuredDimension(w, (w * pct).toInt().coerceAtMost(resources.displayMetrics.heightPixels / 2) + toolbarH.toInt())
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (clipMode || rec.listening) { if (e.action == MotionEvent.ACTION_DOWN) tapClip(e.x, e.y); return true }
        val c = NativeKB.onTouch(e.action, e.x, e.y - toolbarH)
        if (e.action == MotionEvent.ACTION_DOWN && c == 3) { rec.toggle(); return true }
        if (e.action == MotionEvent.ACTION_DOWN && c == 4) { clipMode = true; loadClip(); invalidate(); return true }
        if (e.action == MotionEvent.ACTION_DOWN && c == 5) { txRun(svc, TMUXNEXT); return true }  // ⏭ num-row key: switch to next tmux window/session
        if (e.action == MotionEvent.ACTION_DOWN && c != 0) {
            svc.sendChar(c.toChar())
            handler.removeCallbacks(holdCheck)
            handler.postDelayed(holdCheck, 50)
        } else if (e.action == MotionEvent.ACTION_UP || e.action == MotionEvent.ACTION_CANCEL) {
            handler.removeCallbacks(holdCheck)
        }
        invalidate(); return true
    }

    override fun onDraw(canvas: Canvas) {
        if (clipMode || rec.listening) { drawClip(canvas); return }
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), bgPaint)
        canvas.save(); canvas.translate(0f, toolbarH)
        NativeKB.getBounds(bounds)
        val labels = NativeKB.getLabels()
        val pressed = NativeKB.getPressed()
        val mode = NativeKB.getMode()
        val rowCount = NativeKB.getRowCount()
        val debugRed = prefs.getBoolean("debug_red", true)
        val hf = NativeKB.getHFudge()
        val rowSizes = if (mode == 1) intArrayOf(13, 10, 8, 5) else intArrayOf(13, 10, 9, 9, 4)
        var row = 0; var inRow = 0
        for (i in labels.indices) {
            val b = i * 4
            if (bounds[b+2] - bounds[b] <= 0) continue
            val rf = NativeKB.getRowFudge(row)
            val isPressed = i == pressed
            if (isPressed && debugRed) canvas.drawRoundRect(bounds[b] - hf, bounds[b+1] + rf, bounds[b+2] + hf, bounds[b+3] + rf, 8f, 8f, pressPaint)
            else canvas.drawRoundRect(bounds[b]+4, bounds[b+1]+4, bounds[b+2]-4, bounds[b+3]-4, 8f, 8f, if (isPressed) pressPaint else keyPaint)
            val label = when (val ch = labels[i]) { '\b' -> "⌫"; '\n' -> "↵"; ' ' -> ""; '\u0001' -> if (mode == 1) "ABC" else "?123"; '\u0002' -> "⇧"; '\u0003' -> if (rec.listening) "●" else "🎤"; '\u0004' -> "📋"; '\u0005' -> "⏭"; else -> ch.toString() }
            canvas.drawText(label, (bounds[b] + bounds[b+2]) / 2, (bounds[b+1] + bounds[b+3]) / 2 + 16, textPaint)
            inRow++; if (row < rowSizes.size && inRow >= rowSizes[row]) { row++; inRow = 0 }
        }
        canvas.restore()
    }
}

class SettingsActivity : android.app.Activity() {
    override fun onCreate(b: android.os.Bundle?) {
        super.onCreate(b)
        val prefs = android.preference.PreferenceManager.getDefaultSharedPreferences(this)
        val layout = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.VERTICAL; setPadding(48, 48, 48, 48) }
        layout.addView(android.widget.TextView(this).apply { text = "InstantKB-NDK Termux"; textSize = 18f })
        layout.addView(android.widget.Button(this).apply { text = "Enable Keyboard"; setOnClickListener { startActivity(android.content.Intent(android.provider.Settings.ACTION_INPUT_METHOD_SETTINGS)) } })
        layout.addView(android.widget.TextView(this).apply { text = "\nHeight %"; textSize = 14f })
        val h0 = prefs.getInt("kb_height", 95)
        layout.addView(android.widget.SeekBar(this).apply { max = 180; progress = h0 - 20
            setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: android.widget.SeekBar?, p: Int, u: Boolean) { prefs.edit().putInt("kb_height", p + 20).apply() }
                override fun onStartTrackingTouch(s: android.widget.SeekBar?) {}; override fun onStopTrackingTouch(s: android.widget.SeekBar?) {}
            })
        })
        for (r in 0..4) {
            val f0 = prefs.getInt("fudge$r", 20)
            val row = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.HORIZONTAL }
            row.addView(android.widget.TextView(this).apply { text = "R${r+1} "; textSize = 14f })
            val num = android.widget.EditText(this).apply { inputType = android.text.InputType.TYPE_CLASS_NUMBER; setText(f0.toString()); minWidth = 100 }
            val slider = android.widget.SeekBar(this).apply { max = 60; progress = f0; layoutParams = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
            num.setOnEditorActionListener { _, _, _ -> val v = num.text.toString().toIntOrNull()?.coerceIn(0, 60) ?: 20; num.setText(v.toString()); slider.progress = v; prefs.edit().putInt("fudge$r", v).apply(); NativeKB.setRowFudge(r, v.toFloat()); true }
            slider.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: android.widget.SeekBar?, p: Int, u: Boolean) { num.setText(p.toString()); prefs.edit().putInt("fudge$r", p).apply(); NativeKB.setRowFudge(r, p.toFloat()) }
                override fun onStartTrackingTouch(s: android.widget.SeekBar?) {}; override fun onStopTrackingTouch(s: android.widget.SeekBar?) {}
            })
            row.addView(num); row.addView(slider); layout.addView(row)
        }
        val hf0 = prefs.getInt("hfudge", 10)
        val hrow = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.HORIZONTAL }
        hrow.addView(android.widget.TextView(this).apply { text = "L/R "; textSize = 14f })
        val hnum = android.widget.EditText(this).apply { inputType = android.text.InputType.TYPE_CLASS_NUMBER; setText(hf0.toString()); minWidth = 100 }
        val hslider = android.widget.SeekBar(this).apply { max = 40; progress = hf0; layoutParams = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
        hnum.setOnEditorActionListener { _, _, _ -> val v = hnum.text.toString().toIntOrNull()?.coerceIn(0, 40) ?: 10; hnum.setText(v.toString()); hslider.progress = v; prefs.edit().putInt("hfudge", v).apply(); NativeKB.setHFudge(v.toFloat()); true }
        hslider.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: android.widget.SeekBar?, p: Int, u: Boolean) { hnum.setText(p.toString()); prefs.edit().putInt("hfudge", p).apply(); NativeKB.setHFudge(p.toFloat()) }
            override fun onStartTrackingTouch(s: android.widget.SeekBar?) {}; override fun onStopTrackingTouch(s: android.widget.SeekBar?) {}
        })
        hrow.addView(hnum); hrow.addView(hslider); layout.addView(hrow)
        layout.addView(android.widget.CheckBox(this).apply { text = "Debug: Red flash"; isChecked = prefs.getBoolean("debug_red", true); setOnCheckedChangeListener { _, c -> prefs.edit().putBoolean("debug_red", c).apply() } })
        setContentView(layout)
    }
}

// VoiceLine: a SECOND IME in this same APK (own entry in Android's keyboard picker, label "a voice
// line") — literally ONE LINE tall: a dictation toggle + enter. The point is giving screen height
// back to the content while still allowing input. Continuous dictation à la Cap: partials stream
// into the field as composing text, finalized phrases commit, auto-relisten until tapped off.
class VoiceLineService : InputMethodService() {
    private var v: VoiceLineView? = null
    val rec = Dict(this) { v?.invalidate() }
    override fun onEvaluateFullscreenMode() = false
    override fun onCreateInputView(): View { v = VoiceLineView(this); return v!! }
    override fun onFinishInputView(f: Boolean) { rec.stop(); super.onFinishInputView(f) }
    fun enter() { currentInputConnection?.finishComposingText(); val a = currentInputEditorInfo?.imeOptions?.and(android.view.inputmethod.EditorInfo.IME_MASK_ACTION) ?: 0
        if (a != 0 && a != android.view.inputmethod.EditorInfo.IME_ACTION_NONE) currentInputConnection?.performEditorAction(a) else currentInputConnection?.commitText("\n", 1) }
    fun bksp() { currentInputConnection?.finishComposingText(); currentInputConnection?.deleteSurroundingText(1, 0) }
}
class VoiceLineView(private val svc: VoiceLineService) : View(svc) {
    private val t = Paint().apply { color = Color.WHITE; textSize = 40f; textAlign = Paint.Align.CENTER; isAntiAlias = true; typeface = Typeface.MONOSPACE }
    private val f = Paint()
    override fun onMeasure(ws: Int, hs: Int) = setMeasuredDimension(MeasureSpec.getSize(ws), (resources.displayMetrics.density * 46).toInt())
    override fun onDraw(c: Canvas) { c.drawColor(0xFF000000.toInt()); val h = height.toFloat(); val d = width * 0.56f; val m = width * 0.78f
        if (svc.rec.listening) { f.color = 0xFF7F1010.toInt(); c.drawRect(0f, 0f, d, h, f) }
        c.drawText(if (svc.rec.listening) "● tap to stop" else "🎤 tap to dictate", d / 2f, h / 2f + 14f, t)
        f.color = 0xFF222222.toInt(); c.drawRect(d, 0f, m - 2f, h, f); c.drawRect(m, 0f, width.toFloat(), h, f)
        c.drawText("⌫", (d + m) / 2f, h / 2f + 14f, t); c.drawText("↵", (m + width) / 2f, h / 2f + 14f, t) }
    override fun onTouchEvent(e: MotionEvent): Boolean { if (e.action == MotionEvent.ACTION_DOWN) { when { e.x < width * 0.56f -> svc.rec.toggle(); e.x < width * 0.78f -> svc.bksp(); else -> svc.enter() }; invalidate() }; return true }
}
'''
KC=r'''// Instant Keyboard - Native C implementation
#include <jni.h>
#include <string.h>
#include <time.h>

static const char* ABC[] = {"1234567890\005\004\003","qwertyuiop","asdfghjkl","\002zxcvbnm\b","\001 .\n"};
static const char* SYM[] = {"1234567890\005\004\003","@#$_&-+()/","*\"':;!?\b","\001, .\n"};
static int symMode = 0, shift = 0, pressedKey = -1, kbWidth, kbHeight;
static float keyW, rowH, bounds[48][4], rowFudge[5] = {0}, hFudge = 0;
static long pressTime = 0;
static char pressedChar = 0;

static long now_ms() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec*1000 + t.tv_nsec/1000000; }

static void computeBounds() {
    int rows = symMode ? 4 : 5;
    const char** L = symMode ? SYM : ABC;
    rowH = kbHeight / (float)rows;
    float y = 0; int idx = 0;
    for (int r = 0; r < rows; r++, y += rowH) {
        int n = strlen(L[r]);
        for (int i = 0; i < n; i++) {
            float w = keyW, x = i * keyW;
            int rr = symMode ? r : r - 1; // row index for layout calc (skip num row logic)
            if (r == 0) { float nkw = kbWidth/(float)n; w = nkw; x = i*nkw; } // num row: n keys (10 digits + tmux-next + clipboard + mic) fill width
            else if (rr == 0) { /* qwerty row: simple */ }
            else if (rr == 1 && !symMode) x = keyW * 0.5f + i * keyW;
            else if (rr == 2) {
                if (symMode) { x = keyW * 1.5f + i * keyW; if (i == n-1) { x = keyW * 8.5f; w = keyW * 1.5f; } }
                else { if (i == 0) { w = keyW * 1.5f; } else if (i == n-1) { x = keyW * 8.5f; w = keyW * 1.5f; } else { x = keyW * 1.5f + (i-1) * keyW; } }
            } else if (rr == 3) {
                if (symMode) { w = (i==0) ? keyW*2 : (i==2) ? keyW*4 : (i==4) ? keyW*2 : keyW; }
                else { w = (i==0) ? keyW*2 : (i==1) ? keyW*5 : (i==2) ? keyW*1 : keyW*2; }
                x = 0; for (int j = 0; j < i; j++) { if (symMode) x += (j==0) ? keyW*2 : (j==2) ? keyW*4 : (j==4) ? keyW*2 : keyW; else x += (j==0) ? keyW*2 : (j==1) ? keyW*5 : (j==2) ? keyW*1 : keyW*2; }
            }
            bounds[idx][0] = x; bounds[idx][1] = y; bounds[idx][2] = x + w; bounds[idx][3] = y + rowH; idx++;
        }
    }
}

static int hitTest(float x, float y) {
    int rows = symMode ? 4 : 5;
    const char** L = symMode ? SYM : ABC;
    int idx = 0;
    for (int r = 0; r < rows; r++) {
        int n = strlen(L[r]);
        float ay = y - rowFudge[r];
        for (int i = 0; i < n; i++, idx++)
            if (x >= bounds[idx][0] - hFudge && x <= bounds[idx][2] + hFudge && ay >= bounds[idx][1] && ay <= bounds[idx][3]) return idx;
    }
    return -1;
}

static char getChar(int idx) {
    int rows = symMode ? 4 : 5;
    const char** L = symMode ? SYM : ABC;
    int i = 0;
    for (int r = 0; r < rows; r++) { int n = strlen(L[r]); if (idx < i + n) return L[r][idx - i]; i += n; }
    return 0;
}

JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_init(JNIEnv* e, jclass c, jint w, jint h) { kbWidth = w; kbHeight = h; keyW = w / 10.0f; computeBounds(); }
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_setRowFudge(JNIEnv* e, jclass c, jint row, jfloat fudge) { if (row >= 0 && row < 5) rowFudge[row] = fudge; }
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_setHFudge(JNIEnv* e, jclass c, jfloat fudge) { hFudge = fudge; }
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_getRowCount(JNIEnv* e, jclass c) { return symMode ? 4 : 5; }
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_onTouch(JNIEnv* e, jclass c, jint action, jfloat x, jfloat y) {
    if (action == 0) { pressedKey = hitTest(x, y); pressTime = now_ms(); if (pressedKey >= 0) { pressedChar = getChar(pressedKey); return pressedChar; } }
    else if (action == 1 || action == 3) { pressedKey = -1; pressedChar = 0; }
    return 0;
}
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_checkHold(JNIEnv* e, jclass c) {
    if (pressedKey < 0) return 0;
    long el = now_ms() - pressTime;
    if (pressedChar == '\b' && el > 400) { pressTime = now_ms() - 350; return '\b'; }
    if (pressedChar == '\001' && el > 400) { pressTime += 99999; return 'S'; }
    return 0;
}
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_toggleMode(JNIEnv* e, jclass c) { symMode = !symMode; shift = 0; computeBounds(); }
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_toggleShift(JNIEnv* e, jclass c) { shift = !shift; }
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_clearShift(JNIEnv* e, jclass c) { shift = 0; }
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_getMode(JNIEnv* e, jclass c) { return symMode; }
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_getShift(JNIEnv* e, jclass c) { return shift; }
JNIEXPORT jint JNICALL Java_com_aios_a_NativeKB_getPressed(JNIEnv* e, jclass c) { return pressedKey; }
JNIEXPORT jfloat JNICALL Java_com_aios_a_NativeKB_getHFudge(JNIEnv* e, jclass c) { return hFudge; }
JNIEXPORT jfloat JNICALL Java_com_aios_a_NativeKB_getRowFudge(JNIEnv* e, jclass c, jint row) { return (row >= 0 && row < 5) ? rowFudge[row] : 0; }
JNIEXPORT void JNICALL Java_com_aios_a_NativeKB_getBounds(JNIEnv* e, jclass c, jfloatArray out) { (*e)->SetFloatArrayRegion(e, out, 0, 48*4, (jfloat*)bounds); }
JNIEXPORT jstring JNICALL Java_com_aios_a_NativeKB_getLabels(JNIEnv* e, jclass c) {
    static char buf[64]; int rows = symMode ? 4 : 5; const char** L = symMode ? SYM : ABC; int i = 0;
    for (int r = 0; r < rows; r++) for (int j = 0; L[r][j]; j++) { char ch = L[r][j]; buf[i++] = (shift && ch >= 'a' && ch <= 'z') ? ch - 32 : ch; }
    buf[i] = 0; return (*e)->NewStringUTF(e, buf);
}
'''
RDR_C=r'''// Reader - paginated text reader, kb-style native state + JNI bridges.
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define MAX_PAGES 65536
static char* text = NULL; static int text_len = 0;
static int page_off[MAX_PAGES]; static int n_pages = 0, cur = 0;
static int W = 0, H = 0, chars_per_page = 4000;
static void paginate(void) {
    n_pages = 0;
    if (!text || text_len <= 0) return;
    int i = 0;
    while (i < text_len && n_pages < MAX_PAGES) {
        page_off[n_pages++] = i;
        int target = i + chars_per_page;
        if (target >= text_len) break;
        // Prefer ending at sentence boundary near target to avoid mid-sentence visual cuts.
        // Scan window [target-200, target] for ". " / "! " / "? " or "\n\n"; use latest hit.
        int win = target - 200; if (win < i + 100) win = i + 100;
        int cut = -1;
        for (int j = target; j > win; j--) {
            char c = text[j];
            if (c == '\n' && j+1 < text_len && text[j+1] == '\n') { cut = j+2; break; }
            if ((c == '.' || c == '!' || c == '?') && j+1 < text_len && isspace((unsigned char)text[j+1])) { cut = j+2; break; }
        }
        if (cut < 0) {
            // fallback: any whitespace
            cut = target;
            while (cut > i && !isspace((unsigned char)text[cut])) cut--;
            if (cut == i) cut = target;
            while (cut < text_len && isspace((unsigned char)text[cut])) cut++;
        }
        i = cut;
    }
}
#define RF(ret, name) JNIEXPORT ret JNICALL Java_com_aios_a_Rdr_##name
RF(void, nInit)(JNIEnv* e, jclass c, jint w, jint h, jint cpp) { (void)e;(void)c; W=w; H=h; if(cpp>0) chars_per_page=cpp; paginate(); if(cur>=n_pages) cur = n_pages>0 ? n_pages-1 : 0; }
RF(jint, nLoad)(JNIEnv* e, jclass c, jstring path, jint startPage) {
    (void)c;
    const char* p = (*e)->GetStringUTFChars(e, path, 0);
    int fd = open(p, O_RDONLY);
    (*e)->ReleaseStringUTFChars(e, path, p);
    if (fd < 0) return 0;
    struct stat st; if (fstat(fd, &st) < 0) { close(fd); return 0; }
    if (text) munmap(text, text_len);
    text = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED) { text=NULL; text_len=0; return 0; }
    text_len = (int)st.st_size; paginate();
    cur = (startPage>=0 && startPage<n_pages) ? startPage : 0;
    return n_pages;
}
RF(jstring, nPage)(JNIEnv* e, jclass c) {
    (void)c;
    if (n_pages==0 || !text) return (*e)->NewStringUTF(e, "");
    int s = page_off[cur], en = (cur+1<n_pages) ? page_off[cur+1] : text_len;
    int len = en - s; if (len<=0) return (*e)->NewStringUTF(e, "");
    static char* buf=NULL; static int buf_cap=0;
    if (len+1>buf_cap) { free(buf); buf=malloc(len+1); buf_cap=len+1; }
    memcpy(buf, text+s, len); buf[len]=0;
    return (*e)->NewStringUTF(e, buf);
}
RF(jint, nTouch)(JNIEnv* e, jclass c, jint act, jfloat x, jfloat y) {
    (void)e;(void)c;(void)y;
    if (act != 0) return 0;
    int prev = cur;
    if (x < W/3.0f) { if (cur>0) cur--; } else { if (cur+1<n_pages) cur++; }
    return cur != prev;
}
RF(jint, nGetPage)(JNIEnv* e, jclass c)  { (void)e;(void)c; return cur; }
RF(jint, nGetTotal)(JNIEnv* e, jclass c) { (void)e;(void)c; return n_pages; }
'''
MXML=r'''<?xml version="1.0" encoding="utf-8"?>
<input-method xmlns:android="http://schemas.android.com/apk/res/android"/>
'''
CML='cmake_minimum_required(VERSION 3.22)\nproject(anative)\nadd_compile_options(-O3 -flto)\nadd_link_options(-flto -Wl,-z,max-page-size=16384)\nadd_library(anative SHARED native.c)\ntarget_link_libraries(anative log android)\nadd_library(launcher SHARED launcher.c)\ntarget_link_libraries(launcher log android m)\nadd_library(keyboard SHARED keyboard.c)\ntarget_link_libraries(keyboard log)\nadd_library(reader SHARED reader.c)\ntarget_link_libraries(reader log)\nadd_library(bubble SHARED bubble.c)\ntarget_link_libraries(bubble log m)\n'
BUB_C=r'''#include <jni.h>
#include <math.h>
JNIEXPORT void JNICALL Java_com_aios_a_BubbleService_render(JNIEnv*e,jobject o,jintArray a,jint w,jint h){
 (void)o;jint*p=(*e)->GetIntArrayElements(e,a,NULL);float c=(float)w/2.0f,r=c-2;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){float dx=(float)x-c,dy=(float)y-c;p[y*w+x]=sqrtf(dx*dx+dy*dy)<=r?(jint)0xFFFF2020:0;}
 (*e)->ReleaseIntArrayElements(e,a,p,0);}
'''
MF='<manifest xmlns:android="http://schemas.android.com/apk/res/android"><uses-permission android:name="android.permission.INTERNET"/><uses-permission android:name="com.termux.permission.RUN_COMMAND"/><uses-permission android:name="android.permission.QUERY_ALL_PACKAGES"/><uses-permission android:name="android.permission.RECORD_AUDIO"/><uses-permission android:name="android.permission.SYSTEM_ALERT_WINDOW"/><uses-permission android:name="android.permission.FOREGROUND_SERVICE"/><uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE"/><uses-permission android:name="android.permission.PACKAGE_USAGE_STATS"/><uses-permission android:name="android.permission.RECEIVE_BOOT_COMPLETED"/><uses-permission android:name="com.termux.permission.RUN_COMMAND"/><uses-permission android:name="moe.shizuku.manager.permission.API_V23"/><queries><package android:name="moe.shizuku.privileged.api"/></queries><application android:usesCleartextTraffic="true" android:extractNativeLibs="true" android:networkSecurityConfig="@xml/nsc" android:label="a apk"><provider android:name="rikka.shizuku.ShizukuProvider" android:authorities="com.aios.a.shizuku" android:multiprocess="false" android:enabled="true" android:exported="true" android:permission="android.permission.INTERACT_ACROSS_USERS_FULL"/><activity android:name=".M" android:exported="true" android:launchMode="singleTop" android:windowSoftInputMode="adjustResize"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter><meta-data android:name="android.app.shortcuts" android:resource="@xml/shortcuts"/></activity><activity android:name=".Home" android:exported="true" android:launchMode="singleTask" android:stateNotNeeded="true" android:theme="@style/T"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.HOME"/><category android:name="android.intent.category.DEFAULT"/></intent-filter></activity><service android:name=".InstantNdkService" android:label="a kb" android:permission="android.permission.BIND_INPUT_METHOD" android:exported="true"><intent-filter><action android:name="android.view.InputMethod"/></intent-filter><meta-data android:name="android.view.im" android:resource="@xml/method"/></service><service android:name=".VoiceLineService" android:label="a voice line" android:permission="android.permission.BIND_INPUT_METHOD" android:exported="true"><intent-filter><action android:name="android.view.InputMethod"/></intent-filter><meta-data android:name="android.view.im" android:resource="@xml/method"/></service><activity android:name=".SettingsActivity" android:exported="true"/><activity android:name=".Cap" android:exported="true" android:windowSoftInputMode="stateAlwaysVisible|adjustResize"/><service android:name=".BubbleService" android:exported="false" android:foregroundServiceType="specialUse"><property android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE" android:value="bubble"/></service><service android:name=".Wd" android:exported="false" android:foregroundServiceType="specialUse"><property android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE" android:value="serve"/></service><service android:name=".Sc" android:permission="android.permission.BIND_ACCESSIBILITY_SERVICE" android:exported="true"><intent-filter><action android:name="android.accessibilityservice.AccessibilityService"/></intent-filter><meta-data android:name="android.accessibilityservice" android:resource="@xml/acc"/></service><receiver android:name=".Boot" android:exported="true"><intent-filter><action android:name="android.intent.action.BOOT_COMPLETED"/><action android:name="android.intent.action.MY_PACKAGE_REPLACED"/></intent-filter></receiver></application></manifest>'
NSC='<?xml version="1.0" encoding="utf-8"?><network-security-config><base-config cleartextTrafficPermitted="true"><trust-anchors><certificates src="system"/></trust-anchors></base-config><domain-config cleartextTrafficPermitted="true"><domain includeSubdomains="true">127.0.0.1</domain><domain includeSubdomains="true">localhost</domain></domain-config></network-security-config>'
# launcher deep-links: each web box = manifest shortcut "a <box>" -> .M --es nav <box> (sync with KT `web`); a-Home + Pixel launcher search both surface these
BOX="note task term home proj op dash stream job book docs cloud prompt".split()
SC='<shortcuts xmlns:android="http://schemas.android.com/apk/res/android">'+''.join(f'<shortcut android:shortcutId="{b}" android:shortcutShortLabel="@string/s_{b}"><intent android:action="android.intent.action.MAIN" android:targetPackage="{P}" android:targetClass="{P}.M"><extra android:name="nav" android:value="{b}"/></intent></shortcut>' for b in BOX)+'</shortcuts>'
STR='<resources>'+''.join(f'<string name="s_{b}">a {b}</string>' for b in BOX)+'</resources>'
ACC='<?xml version="1.0" encoding="utf-8"?><accessibility-service xmlns:android="http://schemas.android.com/apk/res/android" android:accessibilityEventTypes="typeWindowStateChanged" android:accessibilityFeedbackType="feedbackGeneric" android:canRetrieveWindowContent="true" android:accessibilityFlags="flagRetrieveInteractiveWindows"/>'
GS='pluginManagement{repositories{google();mavenCentral()};plugins{id("com.android.application") version "8.2.0";id("org.jetbrains.kotlin.android") version "1.9.22"}}\ndependencyResolutionManagement{repositories{google();mavenCentral()}}\ninclude(":app")\n'
H=os.path.expanduser("~");IT=os.path.exists("/data/data/com.termux")
SDK="/data/data/com.termux/files/home/android-sdk" if IT else os.environ.get("ANDROID_HOME") or next((p for p in[H+"/Library/Android/sdk",H+"/Android/Sdk"] if os.path.isdir(p)),H+"/Android/Sdk")
_ND=SDK+"/ndk";_NV=sorted(os.listdir(_ND))[-1] if os.path.isdir(_ND) else None
_NH=os.listdir(f"{_ND}/{_NV}/toolchains/llvm/prebuilt")[0] if _NV else None
_CMK='externalNativeBuild{cmake{path=file("src/main/cpp/CMakeLists.txt")}}\n'
_DF='defaultConfig{applicationId="'+P+'";minSdk=24;targetSdk=34;versionCode=202;ndk{abiFilters+="arm64-v8a"}'+((';externalNativeBuild{cmake{arguments+="-DANDROID_STL=none"}}') if not IT else '')+'}\n'
GB='plugins{id("com.android.application");id("org.jetbrains.kotlin.android")}\nandroid{namespace="'+P+'";compileSdk=34;'+(f'ndkVersion="{_NV}";' if _NV else '')+_DF+('' if IT else _CMK)+'signingConfigs{getByName("debug"){storeFile=file("debug.keystore")}}\ncompileOptions{sourceCompatibility=JavaVersion.VERSION_11;targetCompatibility=JavaVersion.VERSION_11}\nkotlinOptions{jvmTarget="11"}}\ndependencies{implementation("dev.rikka.shizuku:api:13.1.5");implementation("dev.rikka.shizuku:provider:13.1.5")}\n'
R=os.path.dirname(os.path.dirname(os.path.abspath(__file__)));D=R+"/adata/_apk_build"
if not IT:
    for p in[f"/opt/homebrew/opt/openjdk@{v}/libexec/openjdk.jdk/Contents/Home" for v in[21,17]]+[f"/usr/lib/jvm/java-{v}-openjdk-amd64" for v in[21,17]]:
        if os.path.exists(p):os.environ["JAVA_HOME"]=p;break
def w(p,s):os.makedirs(os.path.dirname(p),exist_ok=True);open(p,"w").write(s)
def adb(*a,serial=None):return S.run(["adb"]+(["-s",serial] if serial else[])+list(a),capture_output=True,text=True)
def devlist():return[l.split('\t')[0] for l in adb("devices").stdout.strip().split('\n')[1:] if '\tdevice' in l]
def _self_adb():
    """On Termux, try wireless adb to self-install. Returns serial or None."""
    if not IT:return None
    adb("connect","localhost:5555")
    out=adb("devices").stdout
    for l in out.strip().split('\n')[1:]:
        p=l.split('\t')
        if len(p)<2:continue
        s,st=p[0],p[1]
        if not any(x in s for x in["localhost","127.0.0.1","emulator"]):continue
        if st=="device":return s
        if st=="unauthorized":print("! ADB connected but unauthorized. Tap 'Allow' on the USB debugging dialog, then retry.");return None
    return None
def pick(ds):
    if len(ds)==1:return ds[0]
    for i,d in enumerate(ds):print(f"  {i}: {adb('-s',d,'shell','getprop','ro.product.model').stdout.strip() or d} ({d})")
    return ds[int(input("#: "))]
CP={0xd03:"a53",0xd05:"a55",0xd0b:"a76",0xd0d:"a77",0xd41:"a78",0xd44:"x1",0xd46:"a510",0xd47:"a710",0xd48:"x2",0xd4d:"a715",0xd4e:"x3",0xd80:"a520",0xd81:"a720",0xd82:"x4"}
def detect_cpu(serial):
    best=None
    for l in adb("shell","cat","/proc/cpuinfo",serial=serial).stdout.splitlines():
        if "CPU part" in l:
            c=CP.get(int(l.split(":")[-1].strip(),16))
            if c:best="cortex-"+c
    if best:print("cpu:",best)
    return best
def shizuku_pair():
    import urllib.request,json,re
    print("Phone steps:\n  1. Settings → Developer Options → Wireless Debugging → ON\n  2. Tap 'Pair device with pairing code' → dialog shows pairing port + 6-digit code\n")
    pp=input("Pairing 'IP:port' from dialog: ").strip();code=input("6-digit code: ").strip()
    ip=pp.split(":")[0]
    p=S.Popen(["adb","pair",pp],stdin=S.PIPE,stdout=S.PIPE,stderr=S.STDOUT);o,_=p.communicate(input=(code+"\n").encode())
    print(o.decode());"Successfully paired" in o.decode() or sys.exit("x Pair failed")
    print("→ discovering debug port via mDNS...");S.run(["sleep","2"])
    m=S.run(["adb","mdns","services"],capture_output=True,text=True).stdout
    dp=next((re.search(rf"{re.escape(ip)}:(\d+)",l).group(1) for l in m.splitlines() if ip in l and "_adb-tls-connect" in l),None)
    dp or sys.exit("x mDNS didn't find debug port; ensure Wireless Debugging still ON")
    print(f"→ debug port: {dp}");S.check_call(["adb","connect",f"{ip}:{dp}"])
    sz=S.run(["adb","-s",f"{ip}:{dp}","shell","pm","path","moe.shizuku.privileged.api"],capture_output=True,text=True).stdout.strip()
    if not sz:
        print("→ fetching latest Shizuku APK...")
        r=urllib.request.urlopen("https://api.github.com/repos/RikkaApps/Shizuku/releases/latest");d=json.loads(r.read())
        url=next(a["browser_download_url"] for a in d["assets"] if a["name"].endswith(".apk"))
        urllib.request.urlretrieve(url,"/tmp/shizuku.apk")
        S.check_call(["adb","-s",f"{ip}:{dp}","install","-r","/tmp/shizuku.apk"])
        print("✓ Shizuku installed")
    abi=S.run(["adb","-s",f"{ip}:{dp}","shell","getprop","ro.product.cpu.abi"],capture_output=True,text=True).stdout.strip()
    sub={"arm64-v8a":"arm64","armeabi-v7a":"arm","x86_64":"x86_64","x86":"x86"}.get(abi,"arm64")
    S.check_call(["adb","-s",f"{ip}:{dp}","shell",f'L=$(dirname $(pm path moe.shizuku.privileged.api|head -1|sed s/package://))/lib/{sub}/libshizuku.so; "$L"'])
    print("\n✓ Shizuku service started. After reboot: re-run this command (pairing persists).")
def _rish_install(apk_path,pkg,serial=None):
    """Silent install via Shizuku rish. Works from Linux (adb shell) AND Termux self-install."""
    in_tmx=IT and not serial
    rid="com.termux" if in_tmx else "moe.shizuku.privileged.api"
    R=f"RISH_APPLICATION_ID={rid} /system/bin/sh /data/local/tmp/rish"
    sh=(lambda c:adb("shell",c,serial=serial)) if serial else (lambda c:S.run(["sh","-c",c],capture_output=True,text=True))
    if sh("[ -r /data/local/tmp/rish ] && [ -r /data/local/tmp/rish_shizuku.dex ] && echo OK").stdout.strip()!="OK":
        if in_tmx:print("x rish missing — run 'a apk' from a device with USB/wireless adb first to set up");return False
        p=sh("pm path moe.shizuku.privileged.api 2>/dev/null|head -1|cut -d: -f2").stdout.strip()
        if not p:return False
        sh(f"cd /data/local/tmp && unzip -o '{p}' assets/rish assets/rish_shizuku.dex >/dev/null 2>&1 && mv assets/rish rish 2>/dev/null && mv assets/rish_shizuku.dex rish_shizuku.dex 2>/dev/null; rmdir assets 2>/dev/null; chmod 755 rish 2>/dev/null; chmod 444 rish_shizuku.dex 2>/dev/null")
        if sh("[ -r /data/local/tmp/rish ] && echo OK").stdout.strip()!="OK":return False
    dst=f"/data/local/tmp/{os.path.basename(apk_path)}"
    if serial:
        if adb("push",apk_path,dst,serial=serial).returncode!=0:return False
    elif in_tmx:
        sd=f"/sdcard/Download/{os.path.basename(apk_path)}";os.makedirs("/sdcard/Download",exist_ok=True);shutil.copy(apk_path,sd)
        if sh(f"{R} -c 'cp \"{sd}\" \"{dst}\"'").returncode!=0:return False
    else:
        if S.run(["cp",apk_path,dst]).returncode!=0:return False
    r=sh(f"{R} -c 'pm install -r -t -d -g \"{dst}\"'")
    if "Success" not in (r.stdout or "") and pkg:
        sh(f"{R} -c 'pm uninstall {pkg}'")
        r=sh(f"{R} -c 'pm install -t -d -g \"{dst}\"'")
    if "Success" in (r.stdout or ""):
        # rish pm install -g doesn't actually grant; without RUN_COMMAND the apk can't reach termux at all ("a serve not reachable") — so grant here, not in _provision (noauth skips that)
        if pkg:sh(f"{R} -c 'pm grant {pkg} android.permission.RECORD_AUDIO;pm grant {pkg} com.termux.permission.RUN_COMMAND'")
        if pkg:sh(f"{R} -c 'am start -n {pkg}/.M'" if in_tmx else f"am start -n {pkg}/.M")
        return True
    print(f"x rish: {(r.stdout or '').strip()} {(r.stderr or '').strip()}")
    return False

def qr_pair():
    """One-tap wireless debug pairing via QR scan. No code typing."""
    import secrets,string,re,time
    if not shutil.which("qrencode"):sys.exit("x install qrencode (apt install qrencode / brew install qrencode)")
    svc="studio-"+secrets.token_hex(4);pw="".join(secrets.choice(string.ascii_letters+string.digits) for _ in range(12))
    print("\nPhone: Settings → Developer options → Wireless debugging → Pair device with QR code → scan:\n")
    S.run(["qrencode","-t","ANSIUTF8","-m","1",f"WIFI:T:ADB;S:{svc};P:{pw};;"])
    print(f"\nsvc={svc}  waiting up to 90s for scan...")
    seen=lambda pat:[re.search(r"(\S+):(\d+)",l) for l in S.run(["adb","mdns","services"],capture_output=True,text=True).stdout.splitlines() if pat in l]
    for _ in range(180):
        for m in seen(svc) if any(svc in l for l in S.run(["adb","mdns","services"],capture_output=True,text=True).stdout.splitlines()) else []:
            if not m:continue
            host,port=m.group(1),m.group(2);print(f"\n→ pair {host}:{port}")
            p=S.Popen(["adb","pair",f"{host}:{port}"],stdin=S.PIPE,stdout=S.PIPE,stderr=S.STDOUT);o,_=p.communicate(input=(pw+"\n").encode());print(o.decode())
            if b"Successfully paired" not in o:return
            for _ in range(30):
                for m2 in seen("_adb-tls-connect"):
                    if m2:S.run(["adb","connect",f"{m2.group(1)}:{m2.group(2)}"]);print(f"✓ connected {m2.group(1)}:{m2.group(2)}");return
                time.sleep(0.5)
            return
        time.sleep(0.5)
    print("x timeout")

def _provision(serial,pkg):
    """Push this dev box's gh+rclone creds to the phone so its termux web-UI can sync. adb can't write termux's private home, so we adb-push the files to /data/local/tmp (binary sync — contents never logged, unlike intent extras) and fire --ez prov; only the apk holds termux's RUN_COMMAND grant, so it copies them into ~/.config. Then we wipe the staging copies. Returns provisioned names."""
    import time;staged=[]
    for src,dst,nm in[("~/.config/gh/hosts.yml","/data/local/tmp/a_gh","gh"),("~/.config/rclone/rclone.conf","/data/local/tmp/a_rc","rclone")]:
        p=os.path.expanduser(src)
        if os.path.exists(p) and adb("push",p,dst,serial=serial).returncode==0:
            adb("shell","chmod","644",dst,serial=serial);staged.append((dst,nm))  # 644 so termux (other uid) can read; wiped below
    if not staged:return[]
    if pkg:adb("shell","am","start","-n",pkg+"/.M","--ez","prov","true",serial=serial)
    time.sleep(8)  # let the apk's termux RUN_COMMAND copy them in before we wipe the staging files
    for dst,_ in staged:adb("shell","rm","-f",dst,serial=serial)
    return[nm for _,nm in staged]
def _apk_auth(serial):
    if not serial:
        ds=devlist()
        if not ds:sys.exit("x no device")
        serial=ds[0] if len(ds)==1 else pick(ds)
    names=_provision(serial,P)
    print(f"✓ provisioned termux on {serial}: {', '.join(names)}" if names else "! no gh/rclone creds on this dev box to push")
def _txupdate(serial,pkg):   # pull+rebuild termux a via /api/omni, then restart serve so the new binary is live (a update doesn't reload the running serve); force-stops com.termux. skip: noup
    d=f"adb -s {serial} ";S.run(["sh","-c",d+f"forward tcp:19112 tcp:1112;sleep 3;curl -sm90 localhost:19112/api/omni --data-urlencode q=update;sleep 8;"+d+"shell am force-stop com.termux;"+d+f"shell am start -n {pkg}/.M;"+d+"forward --remove tcp:19112"],capture_output=True);print("→ termux a updated + serve restarted")
def run():
    if "pair" in sys.argv[1:]:return shizuku_pair()
    if "pair-qr" in sys.argv[1:] or "qr" in sys.argv[1:]:return qr_pair()
    if "auth" in sys.argv[1:]:return _apk_auth(next((a for a in sys.argv[2:] if a!="auth"),None))
    auth_on="noauth" not in sys.argv[2:]
    up_on="noup" not in sys.argv[2:]   # default: bring termux a to latest + restart its serve after install
    AMAP={"arm":"armeabi-v7a","v7a":"armeabi-v7a","armeabi-v7a":"armeabi-v7a","arm64":"arm64-v8a","v8a":"arm64-v8a","arm64-v8a":"arm64-v8a"}
    proj=serial=ABI=None
    for a in sys.argv[2:]:
        if a in("noauth","noup"):continue
        if a in AMAP:ABI=AMAP[a];continue
        for p in [a,H+"/"+a,R+"/adata/git/my/"+a]:
            if os.path.isdir(p) and glob.glob(p+"/build.gradle*"):proj=os.path.abspath(p);break
        if proj:break
        elif not serial:serial=a
    if not serial:
        ds=devlist()
        if ds:serial=ds[0] if len(ds)==1 else pick(ds)
    if not ABI:
        pa=adb("shell","getprop","ro.product.cpu.abi",serial=serial).stdout.strip() if serial else ""
        ABI=pa if pa in AMAP else "arm64-v8a"
    TRIPLE={"arm64-v8a":"aarch64-linux-android","armeabi-v7a":"armv7a-linux-androideabi"}[ABI]
    FARCH={"arm64-v8a":"aarch64","armeabi-v7a":"EABI"}[ABI]
    sfx="" if ABI=="arm64-v8a" else "-"+ABI
    SRCD,ADROID="/tmp/dsrc"+sfx,"/tmp/a-droid"+sfx
    print(f"→ target ABI: {ABI}")
    cf="-O3 -flto -Wl,-z,max-page-size=16384"
    if proj:
        if not os.path.exists(proj+"/gradlew"):sys.exit("x No gradlew in "+proj)
        lp=proj+"/local.properties"
        if not os.path.exists(lp) or SDK not in open(lp).read():open(lp,"w").write(f"sdk.dir={SDK}\n")
        ga=["./gradlew","assembleDebug"]
        if cpu:ga.append(f"-Pcpu_target={cpu}")
        os.chdir(proj);S.run(ga,check=True)
        apks=glob.glob(proj+"/**/debug/*.apk",recursive=True)
        if not apks:sys.exit("x No APK")
        apk=apks[0];pkg=None
        for bf in glob.glob(proj+"/app/build.gradle*"):
            for line in open(bf):
                if ("applicationId" in line or "namespace" in line) and '"' in line:pkg=line.split('"')[1];break
    else:
        w(D+"/settings.gradle.kts",GS);w(D+"/app/build.gradle.kts",GB.replace("arm64-v8a",ABI));w(D+"/local.properties",f"sdk.dir={SDK}\n")
        ks=f"{R}/adata/git/common/debug.keystore";hk=f"{H}/.android/debug.keystore"
        if not os.path.exists(ks) and os.path.exists(hk):shutil.copy(hk,ks);print(f"→ seeded {ks}")
        os.makedirs(f"{D}/app",exist_ok=True);shutil.copy(ks,f"{D}/app/debug.keystore")
        gp="android.useAndroidX=true\norg.gradle.jvmargs=-Xmx4g\n"
        if IT:gp+="android.aapt2FromMavenOverride=/data/data/com.termux/files/usr/bin/aapt2\n"
        w(D+"/gradle.properties",gp);w(D+"/app/src/main/AndroidManifest.xml",MF);w(D+"/app/src/main/java/com/aios/a/M.kt",KT);w(D+"/app/src/main/res/xml/nsc.xml",NSC);w(D+"/app/src/main/res/xml/shortcuts.xml",SC);w(D+"/app/src/main/res/values/strings.xml",STR)
        w(D+"/app/src/main/java/com/aios/a/Home.kt",HKT);w(D+"/app/src/main/res/values/styles.xml",TXML)
        w(D+"/app/src/main/java/com/aios/a/InstantNdkService.kt",IKT);w(D+"/app/src/main/res/xml/method.xml",MXML);w(D+"/app/src/main/res/xml/acc.xml",ACC)
        if IT:
            sf=D+"/app/src/main/jniLibs/arm64-v8a";os.makedirs(sf,exist_ok=True)
            w(D+"/native.c",NC);so=sf+"/libanative.so"
            S.run(f"clang -shared {cf} -w -o '{so}' '{D}/native.c'&&patchelf --remove-rpath '{so}'",shell=True,check=True)
            w(D+"/launcher.c",LC);lso=sf+"/liblauncher.so"
            S.run(f"clang -shared {cf} -w -lm -o '{lso}' '{D}/launcher.c'&&patchelf --remove-rpath '{lso}'",shell=True,check=True)
            w(D+"/keyboard.c",KC);kso=sf+"/libkeyboard.so"
            S.run(f"clang -shared {cf} -w -o '{kso}' '{D}/keyboard.c'&&patchelf --remove-rpath '{kso}'",shell=True,check=True)
            w(D+"/reader.c",RDR_C);rso=sf+"/libreader.so"
            S.run(f"clang -shared {cf} -w -o '{rso}' '{D}/reader.c'&&patchelf --remove-rpath '{rso}'",shell=True,check=True)
            w(D+"/bubble.c",BUB_C);bso=sf+"/libbubble.so"
            S.run(f"clang -shared {cf} -w -lm -o '{bso}' '{D}/bubble.c'&&patchelf --remove-rpath '{bso}'",shell=True,check=True)
        else:w(D+"/app/src/main/cpp/native.c",NC);w(D+"/app/src/main/cpp/launcher.c",LC);w(D+"/app/src/main/cpp/keyboard.c",KC);w(D+"/app/src/main/cpp/reader.c",RDR_C);w(D+"/app/src/main/cpp/bubble.c",BUB_C);w(D+"/app/src/main/cpp/CMakeLists.txt",CML.replace("-O3 -flto",cf))
        # Stage bundled bins + terminfo source (from a droid/droidtmux output)
        sf=D+"/app/src/main/jniLibs/"+ABI;os.makedirs(sf,exist_ok=True)
        ad=D+"/app/src/main/assets";os.makedirs(ad,exist_ok=True)
        stage={ADROID:"liba.so",f"{SRCD}/tmux-build-a/tmux":"libtmux.so",f"{SRCD}/ncurses-6.4/progs/tic":"libtic.so"}
        opt={f"{SRCD}/dropbear-2024.86/dbclient":"libssh.so","/tmp/ssh_wrap":"libsshwrap.so"}
        srcmax=max((os.path.getmtime(p) for p in [f"{R}/a.c"]+glob.glob(f"{R}/lib/*.c")+glob.glob(f"{R}/lib/*.h")),default=0)
        miss=[s for s in stage if not os.path.exists(s) or (s==ADROID and os.path.getmtime(s)<srcmax)]
        if ADROID in miss:
            cc=f"{_ND}/{_NV}/toolchains/llvm/prebuilt/{_NH}/bin/{TRIPLE}30-clang"   # 30: bionic statx (feed.c) needs API>=30; fleet is all Android 13+
            S.check_call([cc,'-DSRC="/data/local/tmp"',"-w"]+cf.split()+["-o",ADROID,f"{R}/a.c"])
        if any(SRCD in s for s in miss):S.check_call(["a","droidtmux"],env={**os.environ,"DABI":ABI,"DPUSH":"0"})
        miss=[s for s in stage if not os.path.exists(s)]
        if miss:sys.exit(f"x build failed: {miss}")
        for s,n in {**stage,**{k:v for k,v in opt.items() if os.path.exists(k)}}.items():
            d=f"{sf}/{n}";shutil.copy(s,d)
            if FARCH not in S.run(["file",d],capture_output=True,text=True).stdout:sys.exit(f"x wrong arch: {s} → {d}")
            S.run(["patchelf","--page-size","16384",d],capture_output=True)
        shutil.copy(f"{SRCD}/ncurses-6.4/misc/terminfo.src",f"{ad}/terminfo.src")
        shutil.copy(R+"/lib/ui_full.html",f"{ad}/ui_full.html")
        for sub in["ssh","workspace/projects","workspace/cmds"]:
            sd=R+"/adata/git/"+sub;dd=ad+"/git/"+sub;os.makedirs(dd,exist_ok=True)
            for f in glob.glob(sd+"/*.txt"):shutil.copy(f,dd+"/"+os.path.basename(f))
        if not os.path.exists(D+"/gradle/wrapper/gradle-wrapper.jar"):
            for s in glob.glob(H+"/*/gradlew")+glob.glob(R+"/adata/git/my/*/gradlew"):
                d=os.path.dirname(s);shutil.copy(s,D+"/gradlew");os.chmod(D+"/gradlew",0o755)
                wd=d+"/gradle/wrapper";os.makedirs(D+"/gradle/wrapper",exist_ok=True)
                for f in os.listdir(wd):shutil.copy(wd+"/"+f,D+"/gradle/wrapper/")
                break
            else:sys.exit("x No gradlew")
        os.chdir(D);S.run(["./gradlew","--no-configuration-cache","assembleDebug"],check=True)
        apk=D+"/app/build/outputs/apk/debug/app-debug.apk";pkg=P
    if IT:
        if _rish_install(apk,pkg):print("✓ "+(pkg or os.path.basename(apk))+" (rish)");return
        sa=_self_adb()
        if sa:
            r=adb("install","-r","-g",apk,serial=sa)
            if "INSTALL_FAILED" in r.stdout+r.stderr:
                if pkg:adb("uninstall",pkg,serial=sa)
                r=adb("install","-g",apk,serial=sa)
            if r.returncode==0:
                if pkg:adb("shell","am","start","-n",pkg+"/.M",serial=sa)
                print("✓ "+(pkg or os.path.basename(apk)));return
            print("x adb install failed, falling back to manual")
        S.run(["cp",apk,"/storage/emulated/0/Download/"+os.path.basename(apk)],check=True)
        if not sa:print("! Enable wireless debug for auto-install:\n  Settings → Developer Options → Wireless debugging ON\n  adb connect localhost:5555  → tap Allow\n  APK copied to Downloads")
        if pkg:S.run(["am","start","-n",pkg+"/.M"])
    else:
        if not serial:
            ds=devlist()
            if not ds:sys.exit("No devices")
            serial=pick(ds)
        if _rish_install(apk,pkg,serial=serial):
            names=_provision(serial,pkg) if auth_on else []
            if pkg and up_on:_txupdate(serial,pkg)
            elif not names and pkg:adb("shell","am","start","-n",pkg+"/.M",serial=serial)
            print("✓ "+(pkg or os.path.basename(apk))+" (rish)"+(" + creds: "+", ".join(names) if names else ""));return
        r=adb("install","-r","-g",apk,serial=serial)
        if "INSTALL_FAILED" in r.stdout+r.stderr:
            if pkg:adb("uninstall",pkg,serial=serial)
            r=adb("install","-g",apk,serial=serial)
        if r.returncode:print(r.stderr);sys.exit(1)
        names=_provision(serial,pkg) if auth_on else []
        if pkg and up_on:_txupdate(serial,pkg)
        elif not names and pkg:adb("shell","am","start","-n",pkg+"/.M",serial=serial)
        if names:print("→ provisioned termux creds: "+", ".join(names))
    print("✓ "+(pkg or os.path.basename(apk)))
run()
