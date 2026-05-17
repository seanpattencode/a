"""a apk [path]"""
import os,subprocess as S,shutil,glob,sys
P="com.aios.a"
KT=r'''@file:Suppress("DEPRECATION","OVERRIDE_DEPRECATION")
package com.aios.a
import android.app.Activity;import android.content.*;import android.os.*;import android.webkit.*;import android.view.*;import android.graphics.*;import android.widget.*
import java.io.File;import java.io.OutputStream;import java.net.Socket
private const val U="http://127.0.0.1:1112/term"
class M:Activity(){
companion object{init{System.loadLibrary("anative")}}
private lateinit var w:WebView;private val h=Handler(Looper.getMainLooper());private var n=0
private var wsOut:OutputStream?=null
private val wsExec=java.util.concurrent.Executors.newSingleThreadExecutor()
private fun pg(s:String)=w.loadDataWithBaseURL(null,"<body style='font:18px monospace;padding:20px;background:#000;color:#0f0'>$s","text/html","utf-8",null)
private fun jsEval(s:String)=h.post{w.evaluateJavascript(s,null)}
@JavascriptInterface fun retry(){h.post{boot()}}
@JavascriptInterface fun wsOpen(url:String){wsExec.submit{try{val s=Socket("127.0.0.1",1112);wsOut=s.getOutputStream()
val k=android.util.Base64.encodeToString(ByteArray(16).also{java.security.SecureRandom().nextBytes(it)},android.util.Base64.NO_WRAP)
wsOut!!.write("GET /ws HTTP/1.1\r\nHost: 127.0.0.1:1112\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: $k\r\nSec-WebSocket-Version: 13\r\n\r\n".toByteArray())
val ins=s.getInputStream();val hd=StringBuilder();while(!hd.endsWith("\r\n\r\n")){val b=ins.read();if(b<0)return@submit;hd.append(b.toChar())}
jsEval("window._wsOpen&&window._wsOpen()")
val buf=ByteArray(65536);while(true){val op=ins.read();if(op<0)break;val lb=ins.read();if(lb<0)break;var len=lb and 0x7f
if(len==126){len=(ins.read() shl 8) or ins.read()}else if(len==127){len=0;for(i in 0..7)len=(len shl 8) or ins.read()}
var got=0;while(got<len){val n=ins.read(buf,got,len-got);if(n<=0)return@submit;got+=n}
if((op and 0xf)==8)break
val sb=StringBuilder();for(i in 0 until len)sb.append(buf[i].toInt() and 0xff).append(',')
jsEval("window._wsMsg&&window._wsMsg(String.fromCharCode(${sb.dropLast(1)}))")}
jsEval("window._wsClose&&window._wsClose(1000)")
}catch(e:Exception){jsEval("window._wsClose&&window._wsClose(1011)")}}}
@JavascriptInterface fun wsSend(data:String){wsExec.submit{try{val bs=data.toByteArray(Charsets.UTF_8);val o=java.io.ByteArrayOutputStream();o.write(0x81)
val m=ByteArray(4).also{java.security.SecureRandom().nextBytes(it)}
when{bs.size<126->o.write(0x80 or bs.size);bs.size<65536->{o.write(0x80 or 126);o.write(bs.size shr 8);o.write(bs.size and 0xff)}}
o.write(m);for(i in bs.indices)o.write(bs[i].toInt() xor m[i%4].toInt())
wsOut?.write(o.toByteArray());wsOut?.flush()}catch(e:Exception){}}}
private val SHIM="(function(){var _w=null;window.WebSocket=function(url){_w=this;this.readyState=0;this.send=function(d){A.wsSend(d+'')};this.close=function(){this.readyState=3};A.wsOpen(url)};window._wsOpen=function(){if(_w){_w.readyState=1;if(_w.onopen)_w.onopen()}};window._wsMsg=function(d){if(_w&&_w.onmessage)_w.onmessage({data:d})};window._wsClose=function(c){if(_w){_w.readyState=3;if(_w.onclose)_w.onclose({code:c,wasClean:false})}}})()"
override fun onBackPressed(){if(w.canGoBack())w.goBack() else super.onBackPressed()}
override fun onResume(){super.onResume();boot()}
private val nl by lazy{applicationInfo.nativeLibraryDir}
private fun setup(){val ui=File(filesDir,"lib/ui");ui.mkdirs();val up=File(ui,"ui_full.py");if(!up.exists())assets.open("ui_full.py").use{i->up.outputStream().use{o->i.copyTo(o)}}
val ti=File(filesDir,"terminfo");if(!File(ti,"x/xterm-256color").exists()){ti.deleteRecursively();ti.mkdirs();val src=File(filesDir,"terminfo.src");if(!src.exists())assets.open("terminfo.src").use{i->src.outputStream().use{o->i.copyTo(o)}}
ProcessBuilder("$nl/libtic.so","-o",ti.absolutePath,src.absolutePath).redirectErrorStream(true).redirectOutput(File(filesDir,"tic.log")).start().waitFor()}
val bin=File(filesDir,"bin");bin.mkdirs();for(p in listOf("a" to "liba.so","tmux" to "libtmux.so","tic" to "libtic.so","dbclient" to "libssh.so","ssh" to "libsshwrap.so","sshpass" to "libsshwrap.so","rclone" to "librclone.so")){val l=File(bin,p.first);try{android.system.Os.remove(l.absolutePath)}catch(e:Exception){};try{android.system.Os.symlink("$nl/${p.second}",l.absolutePath)}catch(e:Exception){}}
for(sub in listOf("ssh","workspace/projects","workspace/cmds")){val sd=File(filesDir,"adata/git/$sub");sd.mkdirs();try{for(n in assets.list("git/$sub")?:emptyArray()){assets.open("git/$sub/$n").use{i->File(sd,n).outputStream().use{o->i.copyTo(o)}}}}catch(e:Exception){}}}
private fun spawn(){val pb=ProcessBuilder("$nl/liba.so","serve","1112");pb.environment().apply{put("PATH","${filesDir}/bin:$nl:/system/bin");put("TMUX_BIN","$nl/libtmux.so");put("TIC_BIN","$nl/libtic.so");put("TMUX_TMPDIR",filesDir.absolutePath);put("TERMINFO","${filesDir}/terminfo");put("HOME",filesDir.absolutePath);put("A_SDIR",filesDir.absolutePath)};pb.redirectErrorStream(true);pb.redirectOutput(File(filesDir,"serve.log"));try{pb.start()}catch(x:Exception){pg("spawn failed: $x")}}
private fun boot(){setup();spawn();n=0;h.postDelayed({w.loadUrl(U)},1800)}
private fun atlas(sz:Float):IntArray{val p=Paint().apply{textSize=sz;color=-1;isAntiAlias=true;typeface=Typeface.MONOSPACE};val cw=p.measureText("M").toInt()+1;val ch=(-p.ascent()+p.descent()).toInt()+1;val b=Bitmap.createBitmap(cw*95,ch,Bitmap.Config.ARGB_8888);Canvas(b).let{c->for(i in 0 until 95)c.drawText(((32+i).toChar()).toString(),(i*cw).toFloat(),-p.ascent(),p)};val r=IntArray(2+cw*95*ch);r[0]=cw;r[1]=ch;b.getPixels(r,2,cw*95,0,0,cw*95,ch);b.recycle();return r}
@android.annotation.SuppressLint("ClickableViewAccessibility")
override fun onCreate(b:Bundle?){super.onCreate(b)
WebView.setWebContentsDebuggingEnabled(true)
w=WebView(this).apply{settings.javaScriptEnabled=true;addJavascriptInterface(this@M,"A")
webChromeClient=object:WebChromeClient(){override fun onConsoleMessage(m:ConsoleMessage):Boolean{android.util.Log.w("AWV","[${m.messageLevel()}] ${m.message()} @ ${m.sourceId()}:${m.lineNumber()}");return true}}
webViewClient=object:WebViewClient(){override fun onPageFinished(v:WebView,url:String){v.evaluateJavascript(SHIM,null)}
override fun onReceivedError(v:WebView,r:WebResourceRequest,e:WebResourceError){if(r.isForMainFrame){if(n++<8){pg("<h2>Starting a serve...</h2>$n/8");h.postDelayed({v.loadUrl(U)},1500)}else pg("<h2>a serve not reachable</h2><button onclick='A.retry()'>Retry</button>")}}}}
val nv=T(this);val nt=N(this);val fr=FrameLayout(this);fr.addView(w);fr.addView(nv);fr.addView(nt);w.visibility=View.GONE;nt.visibility=View.GONE
val mb=S(this,listOf("Native" to{w.visibility=View.GONE;nv.visibility=View.VISIBLE;nt.visibility=View.GONE;nv.invalidate()},"Web" to{w.visibility=View.VISIBLE;nv.visibility=View.GONE;nt.visibility=View.GONE},"Notes" to{w.visibility=View.GONE;nv.visibility=View.GONE;nt.visibility=View.VISIBLE;nt.invalidate()}),{fr.visibility=View.INVISIBLE},{fr.visibility=View.VISIBLE})
val root=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setBackgroundColor(0xFF000000.toInt())}
root.addView(fr,LinearLayout.LayoutParams(-1,0,1f));root.addView(mb,LinearLayout.LayoutParams(-1,-2));setContentView(root)}}
class S(val a:Activity,val it:List<Pair<String,()->Unit>>,val onOp:()->Unit={},val onCl:()->Unit={}):FrameLayout(a){var i=0;var o=false
private val p=Paint().apply{textSize=100f;textAlign=Paint.Align.CENTER;typeface=Typeface.MONOSPACE;isAntiAlias=true}
private val et=android.widget.EditText(a).apply{alpha=0f;background=null;inputType=android.text.InputType.TYPE_CLASS_TEXT;imeOptions=android.view.inputmethod.EditorInfo.IME_ACTION_DONE}
private val imm by lazy{a.getSystemService(Context.INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager}
private fun ms()=et.text.toString().replace("\n","").lowercase().let{q->it.indices.filter{j->it[j].first.lowercase().contains(q)}}
private fun cl(){o=false;imm.hideSoftInputFromWindow(et.windowToken,0);et.setText("");onCl();requestLayout();invalidate()}
private fun sel(){val m=ms();if(m.isNotEmpty()){i=m[0];it[i].second()};cl()}
private val v=object:android.view.View(a){
override fun onDraw(cv:Canvas){cv.drawColor(0xFF000000.toInt());if(o){val m=ms();for((r,j) in m.withIndex()){p.color=if(r==0)0xFFFFFF00.toInt() else -1;cv.drawText(it[j].first,width/2f,height-r*200f-70f,p)}}else{p.color=-1;cv.drawText("≡ "+it[i].first,width/2f,85f,p)}}
override fun onTouchEvent(e:MotionEvent):Boolean{if(e.action==MotionEvent.ACTION_DOWN){if(o){val r=((height-e.y)/200).toInt();val m=ms();if(r in m.indices){i=m[r];it[i].second()};cl()}else{o=true;onOp();et.requestFocus();imm.showSoftInput(et,android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT);requestLayout();invalidate()}};return true}}
init{addView(v,FrameLayout.LayoutParams(-1,-1));addView(et,FrameLayout.LayoutParams(1,1));et.addTextChangedListener(object:android.text.TextWatcher{override fun afterTextChanged(s:android.text.Editable?){if(s?.contains('\n')==true)sel() else{v.invalidate();requestLayout()}};override fun beforeTextChanged(s:CharSequence?,x:Int,y:Int,z:Int){};override fun onTextChanged(s:CharSequence?,x:Int,y:Int,z:Int){}});et.setOnEditorActionListener{_,_,_->sel();true};et.setOnKeyListener{_,k,e->if(e.action==android.view.KeyEvent.ACTION_DOWN&&k==android.view.KeyEvent.KEYCODE_ENTER){sel();true}else false}}
override fun onMeasure(ws:Int,hs:Int){val h=if(o)ms().size.coerceAtLeast(1)*200+20 else 140;super.onMeasure(ws,MeasureSpec.makeMeasureSpec(h,MeasureSpec.EXACTLY))}}
class T(c:android.content.Context):android.view.View(c){
private val h=Handler(Looper.getMainLooper())
private var px:IntArray?=null
private lateinit var bmp:Bitmap
private var started=false
private external fun nResize(w:Int,hh:Int)
private external fun nFont(d:IntArray)
private external fun nRender(p:IntArray)
private external fun nTouch(a:Int,x:Float,y:Float)
private external fun nStart(ld:String,fd:String)
private external fun nStop()
private external fun nDirty():Boolean
private val tk=object:Runnable{override fun run(){if(::bmp.isInitialized&&nDirty())invalidate();h.postDelayed(this,16)}}
override fun onAttachedToWindow(){super.onAttachedToWindow();h.post(tk)}
override fun onDetachedFromWindow(){if(started)nStop();h.removeCallbacks(tk);super.onDetachedFromWindow()}
override fun onSizeChanged(w:Int,hh:Int,ow:Int,oh:Int){
if(::bmp.isInitialized)bmp.recycle()
bmp=Bitmap.createBitmap(w,hh,Bitmap.Config.ARGB_8888)
px=IntArray(w*hh);nResize(w,hh)
if(!started){nFont(atlas(40f));nStart(context.applicationInfo.nativeLibraryDir,context.filesDir.absolutePath);started=true}}
override fun onDraw(c:Canvas){val a=px?:return;if(!::bmp.isInitialized)return
nRender(a);bmp.setPixels(a,0,width,0,0,width,height);c.drawBitmap(bmp,0f,0f,null)}
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
class N(c:android.content.Context):android.view.View(c),android.speech.RecognitionListener{
private val sr by lazy{android.speech.SpeechRecognizer.createSpeechRecognizer(c).apply{setRecognitionListener(this@N)}}
private val p=Paint().apply{color=-1;textSize=60f;textAlign=Paint.Align.CENTER;isAntiAlias=true;typeface=Typeface.MONOSPACE}
private var msg="tap to speak"
override fun onDraw(cv:Canvas){cv.drawColor(0xFF000000.toInt());cv.drawText(msg,width/2f,height/2f,p)}
override fun onTouchEvent(e:MotionEvent):Boolean{if(e.action==MotionEvent.ACTION_DOWN){try{sr.cancel();val i=Intent(android.speech.RecognizerIntent.ACTION_RECOGNIZE_SPEECH);i.putExtra(android.speech.RecognizerIntent.EXTRA_LANGUAGE_MODEL,android.speech.RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);i.putExtra(android.speech.RecognizerIntent.EXTRA_PARTIAL_RESULTS,true);msg="listening...";invalidate();sr.startListening(i)}catch(e:Exception){msg="err: ${e.message}";invalidate()}};return true}
override fun onResults(b:Bundle?){val r=b?.getStringArrayList(android.speech.SpeechRecognizer.RESULTS_RECOGNITION);if(r!=null&&r.isNotEmpty()){val t=r[0];try{val nl=context.applicationInfo.nativeLibraryDir;val pb=ProcessBuilder("$nl/liba.so","n",t);pb.environment().apply{put("PATH","${context.filesDir}/bin:$nl:/system/bin");put("HOME",context.filesDir.absolutePath);put("A_SDIR",context.filesDir.absolutePath)};pb.redirectErrorStream(true).start();msg="$t ✓"}catch(e:Exception){msg="$t ✗ ${e.message}"};invalidate()}}
override fun onPartialResults(b:Bundle?){b?.getStringArrayList(android.speech.SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.let{msg=it;invalidate()}}
override fun onError(c:Int){msg="err $c";invalidate()}
override fun onReadyForSpeech(b:Bundle?){};override fun onBeginningOfSpeech(){};override fun onRmsChanged(r:Float){};override fun onBufferReceived(b:ByteArray?){};override fun onEndOfSpeech(){};override fun onEvent(c:Int,b:Bundle?){}
}
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
 "zxcvbnm,.\x08",
 "-= \x09"};
static float KB[60][4];
static int nkeys,pressed=-1;
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
 float kh=H*0.40f,ky=H-kh,rh=kh/NROWS;nkeys=0;
 for(int r=0;r<NROWS;r++){
  int n=(int)strlen(KR[r]);float y=ky+r*rh,kw=W/10.0f;
  for(int i=0;i<n;i++){
   float x,w;
   if(r==0){kw=W/(float)n;x=i*kw;w=kw;}
   else if(r==3){x=kw*0.5f+i*kw;w=kw;}
   else if(r==5){float pos[]={0,kw,2*kw,9*kw},wd[]={kw,kw,7*kw,W-9*kw};x=pos[i];w=wd[i];}
   else{x=i*kw;w=kw;}
   KB[nkeys][0]=x;KB[nkeys][1]=y;KB[nkeys][2]=x+w;KB[nkeys][3]=y+rh;nkeys++;}}}
static void compute_grid(void){
 if(!FN.cw||!FN.ch){rows=24;cols=80;return;}
 int kh=(int)(H*0.40f);
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
   int tx=ix+(iw-lw)/2,ty=iy+(ih-FN.ch)/2;
   drawstr((uint32_t*)p,stride,&FN,lb,tx,ty,col);
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
CML='cmake_minimum_required(VERSION 3.22)\nproject(anative)\nadd_compile_options(-O3 -flto)\nadd_link_options(-flto -Wl,-z,max-page-size=16384)\nadd_library(anative SHARED native.c)\ntarget_link_libraries(anative log android)\nadd_library(launcher SHARED launcher.c)\ntarget_link_libraries(launcher log android m)\nadd_library(keyboard SHARED keyboard.c)\ntarget_link_libraries(keyboard log)\n'
MF='<manifest xmlns:android="http://schemas.android.com/apk/res/android"><uses-permission android:name="android.permission.INTERNET"/><uses-permission android:name="android.permission.QUERY_ALL_PACKAGES"/><uses-permission android:name="android.permission.RECORD_AUDIO"/><uses-permission android:name="moe.shizuku.manager.permission.API_V23"/><queries><package android:name="moe.shizuku.privileged.api"/></queries><application android:usesCleartextTraffic="true" android:extractNativeLibs="true" android:networkSecurityConfig="@xml/nsc" android:label="a apk"><provider android:name="rikka.shizuku.ShizukuProvider" android:authorities="com.aios.a.shizuku" android:multiprocess="false" android:enabled="true" android:exported="true" android:permission="android.permission.INTERACT_ACROSS_USERS_FULL"/><activity android:name=".M" android:exported="true" android:windowSoftInputMode="adjustResize"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity><activity android:name=".Home" android:exported="true" android:launchMode="singleTask" android:stateNotNeeded="true" android:theme="@style/T"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.HOME"/><category android:name="android.intent.category.DEFAULT"/></intent-filter></activity><service android:name=".InstantNdkService" android:permission="android.permission.BIND_INPUT_METHOD" android:exported="true"><intent-filter><action android:name="android.view.InputMethod"/></intent-filter><meta-data android:name="android.view.im" android:resource="@xml/method"/></service><activity android:name=".SettingsActivity" android:exported="true"/></application></manifest>'
NSC='<?xml version="1.0" encoding="utf-8"?><network-security-config><base-config cleartextTrafficPermitted="true"><trust-anchors><certificates src="system"/></trust-anchors></base-config><domain-config cleartextTrafficPermitted="true"><domain includeSubdomains="true">127.0.0.1</domain><domain includeSubdomains="true">localhost</domain></domain-config></network-security-config>'
GS='pluginManagement{repositories{google();mavenCentral()};plugins{id("com.android.application") version "8.2.0";id("org.jetbrains.kotlin.android") version "1.9.22"}}\ndependencyResolutionManagement{repositories{google();mavenCentral()}}\ninclude(":app")\n'
H=os.path.expanduser("~");IT=os.path.exists("/data/data/com.termux")
SDK="/data/data/com.termux/files/home/android-sdk" if IT else os.environ.get("ANDROID_HOME") or next((p for p in[H+"/Library/Android/sdk",H+"/Android/Sdk"] if os.path.isdir(p)),H+"/Android/Sdk")
_ND=SDK+"/ndk";_NV=sorted(os.listdir(_ND))[-1] if os.path.isdir(_ND) else None
_NH=os.listdir(f"{_ND}/{_NV}/toolchains/llvm/prebuilt")[0] if _NV else None
_CMK='externalNativeBuild{cmake{path=file("src/main/cpp/CMakeLists.txt")}}\n'
_DF='defaultConfig{applicationId="'+P+'";minSdk=24;targetSdk=34;versionCode=202;ndk{abiFilters+="arm64-v8a"}'+((';externalNativeBuild{cmake{arguments+="-DANDROID_STL=none"}}') if not IT else '')+'}\n'
GB='plugins{id("com.android.application");id("org.jetbrains.kotlin.android")}\nandroid{namespace="'+P+'";compileSdk=34;'+(f'ndkVersion="{_NV}";' if _NV else '')+_DF+('' if IT else _CMK)+'compileOptions{sourceCompatibility=JavaVersion.VERSION_11;targetCompatibility=JavaVersion.VERSION_11}\nkotlinOptions{jvmTarget="11"}}\ndependencies{implementation("dev.rikka.shizuku:api:13.1.5");implementation("dev.rikka.shizuku:provider:13.1.5")}\n'
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
def run():
    if "pair" in sys.argv[1:]:return shizuku_pair()
    proj=serial=None
    for a in sys.argv[2:]:
        for p in [a,H+"/"+a,R+"/adata/git/my/"+a]:
            if os.path.isdir(p) and glob.glob(p+"/build.gradle*"):proj=os.path.abspath(p);break
        if proj:break
        elif not serial:serial=a
    if not serial:
        ds=devlist()
        if ds:serial=ds[0] if len(ds)==1 else pick(ds)
    cpu=detect_cpu(serial) if serial else None
    cf="-O3 -flto -Wl,-z,max-page-size=16384"+(f" -mcpu={cpu}" if cpu else "")
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
        w(D+"/settings.gradle.kts",GS);w(D+"/app/build.gradle.kts",GB);w(D+"/local.properties",f"sdk.dir={SDK}\n")
        gp="android.useAndroidX=true\norg.gradle.jvmargs=-Xmx4g\n"
        if IT:gp+="android.aapt2FromMavenOverride=/data/data/com.termux/files/usr/bin/aapt2\n"
        w(D+"/gradle.properties",gp);w(D+"/app/src/main/AndroidManifest.xml",MF);w(D+"/app/src/main/java/com/aios/a/M.kt",KT);w(D+"/app/src/main/res/xml/nsc.xml",NSC)
        TLS=R+"/adata/git/my/text-launcher/app/src/main"
        HKT=open(TLS+"/java/com/tl/M.kt").read().replace("package com.tl","package com.aios.a").replace("class M :","class Home :").replace("this@M","this@Home")
        LC=open(TLS+"/cpp/launcher.c").read().replace("Java_com_tl_M_","Java_com_aios_a_Home_")
        w(D+"/app/src/main/java/com/aios/a/Home.kt",HKT);w(D+"/app/src/main/res/values/styles.xml",open(TLS+"/res/values/t.xml").read())
        NKB=R+"/adata/git/my/instant-keyboard-ndk/app/src/main"
        IKT=open(NKB+"/java/com/aios/nkb/InstantNdkService.kt").read().replace("package com.aios.nkb","package com.aios.a")
        KC=open(NKB+"/cpp/keyboard.c").read().replace("Java_com_aios_nkb_NativeKB_","Java_com_aios_a_NativeKB_")
        w(D+"/app/src/main/java/com/aios/a/InstantNdkService.kt",IKT);w(D+"/app/src/main/res/xml/method.xml",open(NKB+"/res/xml/method.xml").read())
        if IT:
            sf=D+"/app/src/main/jniLibs/arm64-v8a";os.makedirs(sf,exist_ok=True)
            w(D+"/native.c",NC);so=sf+"/libanative.so"
            S.run(f"clang -shared {cf} -w -o '{so}' '{D}/native.c'&&patchelf --remove-rpath '{so}'",shell=True,check=True)
            w(D+"/launcher.c",LC);lso=sf+"/liblauncher.so"
            S.run(f"clang -shared {cf} -w -lm -o '{lso}' '{D}/launcher.c'&&patchelf --remove-rpath '{lso}'",shell=True,check=True)
            w(D+"/keyboard.c",KC);kso=sf+"/libkeyboard.so"
            S.run(f"clang -shared {cf} -w -o '{kso}' '{D}/keyboard.c'&&patchelf --remove-rpath '{kso}'",shell=True,check=True)
        else:w(D+"/app/src/main/cpp/native.c",NC);w(D+"/app/src/main/cpp/launcher.c",LC);w(D+"/app/src/main/cpp/keyboard.c",KC);w(D+"/app/src/main/cpp/CMakeLists.txt",CML.replace("-O3 -flto",cf))
        # Stage bundled bins + terminfo source (from a droid/droidtmux output)
        sf=D+"/app/src/main/jniLibs/arm64-v8a";os.makedirs(sf,exist_ok=True)
        ad=D+"/app/src/main/assets";os.makedirs(ad,exist_ok=True)
        stage={"/tmp/a-droid":"liba.so","/tmp/dsrc/tmux-build-a/tmux":"libtmux.so","/tmp/dsrc/ncurses-6.4/progs/tic":"libtic.so"}
        opt={"/tmp/dsrc/dropbear-2024.86/dbclient":"libssh.so","/tmp/ssh_wrap":"libsshwrap.so"}
        miss=[s for s in stage if not os.path.exists(s)]
        if "/tmp/a-droid" in miss:
            cc=f"{_ND}/{_NV}/toolchains/llvm/prebuilt/{_NH}/bin/aarch64-linux-android29-clang"
            S.check_call([cc,'-DSRC="/data/local/tmp"',"-w"]+cf.split()+["-o","/tmp/a-droid",f"{R}/a.c"])
        if any("/tmp/dsrc" in s for s in miss):S.check_call(["a","droidtmux"])
        miss=[s for s in stage if not os.path.exists(s)]
        if miss:sys.exit(f"x build failed: {miss}")
        for s,n in {**stage,**{k:v for k,v in opt.items() if os.path.exists(k)}}.items():shutil.copy(s,f"{sf}/{n}")
        shutil.copy("/tmp/dsrc/ncurses-6.4/misc/terminfo.src",f"{ad}/terminfo.src")
        shutil.copy(R+"/lib/ui/ui_full.py",f"{ad}/ui_full.py")
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
        r=adb("install","-r","-g",apk,serial=serial)
        if "INSTALL_FAILED" in r.stdout+r.stderr:
            if pkg:adb("uninstall",pkg,serial=serial)
            r=adb("install","-g",apk,serial=serial)
        if r.returncode:print(r.stderr);sys.exit(1)
        if pkg:adb("shell","am","start","-n",pkg+"/.M",serial=serial)
    print("✓ "+(pkg or os.path.basename(apk)))
run()
